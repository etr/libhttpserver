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
#include <memory>
#include <sstream>
#include <string>

#include <httpserver.hpp>

// This example demonstrates how to use get_args() and get_args_flat() to
// process all query string and body arguments from an HTTP request.
//
// Try these URLs:
//   http://localhost:8080/args?name=john&age=30
//   http://localhost:8080/args?id=1&id=2&id=3  (multiple values for same key)
//   http://localhost:8080/args?colors=red&colors=green&colors=blue

class args_resource : public httpserver::http_resource {
 public:
    std::shared_ptr<httpserver::http_response> render(const httpserver::http_request& req) {
        std::stringstream response_body;

        response_body << "=== Using get_args() (supports multiple values per key) ===\n\n";

        // get_args() returns a map where each key maps to an http_arg_value.
        // http_arg_value contains a vector of values for parameters like "?id=1&id=2&id=3"
        auto args = req.get_args();
        for (const auto& [key, arg_value] : args) {
            response_body << "Key: " << key << "\n";
            // Use get_all_values() to get all values for this key
            auto all_values = arg_value.get_all_values();
            if (all_values.size() > 1) {
                response_body << "  Values (" << all_values.size() << "):\n";
                for (const auto& v : all_values) {
                    response_body << "    - " << v << "\n";
                }
            } else {
                // For single values, http_arg_value converts to string_view
                response_body << "  Value: " << std::string_view(arg_value) << "\n";
            }
        }

        response_body << "\n=== Using get_args_flat() (one value per key) ===\n\n";

        // get_args_flat() returns a simple map with one value per key.
        // If a key has multiple values, only the first value is returned.
        auto args_flat = req.get_args_flat();
        for (const auto& [key, value] : args_flat) {
            response_body << key << " = " << value << "\n";
        }

        response_body << "\n=== Accessing individual arguments ===\n\n";

        // You can also access individual arguments directly
        auto name = req.get_arg("name");  // Returns http_arg_value (may have multiple values)
        auto name_flat = req.get_arg_flat("name");  // Returns string_view (first value only)

        if (!name.get_flat_value().empty()) {
            response_body << "name (via get_arg): " << std::string_view(name) << "\n";
        }
        if (!name_flat.empty()) {
            response_body << "name (via get_arg_flat): " << name_flat << "\n";
        }

        return std::make_shared<httpserver::string_response>(response_body.str(), 200, "text/plain");
    }
};

int main() {
    httpserver::webserver ws = httpserver::create_webserver(8080);

    args_resource ar;
    ws.register_resource("/args", &ar);

    std::cout << "Server running on http://localhost:8080/args\n";
    std::cout << "Try: http://localhost:8080/args?name=john&age=30\n";
    std::cout << "Or:  http://localhost:8080/args?id=1&id=2&id=3\n";

    ws.start(true);

    return 0;
}
