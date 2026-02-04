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

/**
 * Example demonstrating client certificate (mTLS) authentication.
 *
 * This example shows how to:
 * 1. Configure the server to request client certificates
 * 2. Extract client certificate information in request handlers
 * 3. Implement certificate-based access control
 *
 * To test this example:
 *
 * 1. Generate server certificate and key:
 *    openssl req -x509 -newkey rsa:2048 -keyout server_key.pem -out server_cert.pem \
 *        -days 365 -nodes -subj "/CN=localhost"
 *
 * 2. Generate a CA certificate for client certs:
 *    openssl req -x509 -newkey rsa:2048 -keyout ca_key.pem -out ca_cert.pem \
 *        -days 365 -nodes -subj "/CN=Test CA"
 *
 * 3. Generate client certificate signed by the CA:
 *    openssl req -newkey rsa:2048 -keyout client_key.pem -out client_csr.pem \
 *        -nodes -subj "/CN=Alice/O=Engineering"
 *    openssl x509 -req -in client_csr.pem -CA ca_cert.pem -CAkey ca_key.pem \
 *        -CAcreateserial -out client_cert.pem -days 365
 *
 * 4. Run the server:
 *    ./client_cert_auth
 *
 * 5. Test with curl using client certificate:
 *    curl -k --cert client_cert.pem --key client_key.pem https://localhost:8443/secure
 *
 *    Or without a certificate (will be denied):
 *    curl -k https://localhost:8443/secure
 */

#include <iostream>
#include <memory>
#include <set>
#include <string>

#include <httpserver.hpp>

// Set of allowed certificate fingerprints (SHA-256, hex-encoded)
// In a real application, this would be loaded from a database or config file
std::set<std::string> allowed_fingerprints;

// Resource that requires client certificate authentication
class secure_resource : public httpserver::http_resource {
 public:
    std::shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request& req) {
        // Check if client provided a certificate
        if (!req.has_client_certificate()) {
            return std::make_shared<httpserver::string_response>(
                "Client certificate required",
                httpserver::http::http_utils::http_unauthorized, "text/plain");
        }

        // Get certificate information
        std::string cn = req.get_client_cert_cn();
        std::string dn = req.get_client_cert_dn();
        std::string issuer = req.get_client_cert_issuer_dn();
        std::string fingerprint = req.get_client_cert_fingerprint_sha256();
        bool verified = req.is_client_cert_verified();

        // Check if certificate is verified by our CA
        if (!verified) {
            return std::make_shared<httpserver::string_response>(
                "Certificate not verified by trusted CA",
                httpserver::http::http_utils::http_forbidden, "text/plain");
        }

        // Optional: Check fingerprint against allowlist
        if (!allowed_fingerprints.empty() &&
            allowed_fingerprints.find(fingerprint) == allowed_fingerprints.end()) {
            return std::make_shared<httpserver::string_response>(
                "Certificate not in allowlist",
                httpserver::http::http_utils::http_forbidden, "text/plain");
        }

        // Check certificate validity times
        time_t now = time(nullptr);
        time_t not_before = req.get_client_cert_not_before();
        time_t not_after = req.get_client_cert_not_after();

        if (now < not_before) {
            return std::make_shared<httpserver::string_response>(
                "Certificate not yet valid",
                httpserver::http::http_utils::http_forbidden, "text/plain");
        }

        if (now > not_after) {
            return std::make_shared<httpserver::string_response>(
                "Certificate has expired",
                httpserver::http::http_utils::http_forbidden, "text/plain");
        }

        // Build response with certificate info
        std::string response = "Welcome, " + cn + "!\n\n";
        response += "Certificate Details:\n";
        response += "  Subject DN: " + dn + "\n";
        response += "  Issuer DN:  " + issuer + "\n";
        response += "  Fingerprint (SHA-256): " + fingerprint + "\n";
        response += "  Verified: " + std::string(verified ? "Yes" : "No") + "\n";

        return std::make_shared<httpserver::string_response>(response, 200, "text/plain");
    }
};

// Public resource that shows certificate info but doesn't require it
class info_resource : public httpserver::http_resource {
 public:
    std::shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request& req) {
        std::string response;

        if (req.has_client_certificate()) {
            response = "Client certificate detected:\n";
            response += "  Common Name: " + req.get_client_cert_cn() + "\n";
            response += "  Verified: " + std::string(req.is_client_cert_verified() ? "Yes" : "No") + "\n";
        } else {
            response = "No client certificate provided.\n";
            response += "Use --cert and --key with curl to provide one.\n";
        }

        return std::make_shared<httpserver::string_response>(response, 200, "text/plain");
    }
};

int main() {
    std::cout << "Starting HTTPS server with client certificate authentication on port 8443...\n";
    std::cout << "\nEndpoints:\n";
    std::cout << "  /info   - Shows certificate info (optional cert)\n";
    std::cout << "  /secure - Requires valid client certificate\n\n";

    // Create webserver with SSL and client certificate trust store
    httpserver::webserver ws = httpserver::create_webserver(8443)
        .use_ssl()
        .https_mem_key("server_key.pem")      // Server private key
        .https_mem_cert("server_cert.pem")    // Server certificate
        .https_mem_trust("ca_cert.pem");      // CA certificate for verifying client certs

    secure_resource secure;
    info_resource info;

    ws.register_resource("/secure", &secure);
    ws.register_resource("/info", &info);

    std::cout << "Server started. Press Ctrl+C to stop.\n\n";
    std::cout << "Test commands:\n";
    std::cout << "  curl -k https://localhost:8443/info\n";
    std::cout << "  curl -k --cert client_cert.pem --key client_key.pem https://localhost:8443/info\n";
    std::cout << "  curl -k --cert client_cert.pem --key client_key.pem https://localhost:8443/secure\n";

    ws.start(true);

    return 0;
}
