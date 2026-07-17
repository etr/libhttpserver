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

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <winsock2.h>
#include <ws2tcpip.h>
#define _WINDOWS
#else
#if defined(__CYGWIN__)
#include <sys/select.h>
#endif
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

#include <errno.h>
#include <microhttpd.h>
#ifdef HAVE_WEBSOCKET
#include <microhttpd_ws.h>
#endif  // HAVE_WEBSOCKET
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <algorithm>
#include <cstring>
#include <iosfwd>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "httpserver/constants.hpp"
#include "httpserver/create_webserver.hpp"
#include "httpserver/feature_unavailable.hpp"
#include "httpserver/websocket_handler.hpp"
#include "httpserver/detail/http_endpoint.hpp"
#include "httpserver/detail/lambda_resource.hpp"
#include "httpserver/detail/modded_request.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/string_utilities.hpp"
#include "httpserver/detail/body.hpp"

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#endif  // HAVE_GNUTLS

using std::string;
using std::pair;
using std::vector;
using std::map;
using std::set;

namespace httpserver {

using httpserver::http::http_utils;
using httpserver::http::ip_representation;
using httpserver::http::base_unescaper;


namespace detail {

void webserver_impl::request_completed(void *cls, struct MHD_Connection *connection, void **con_cls, enum MHD_RequestTerminationCode toe) {
    // These parameters are passed to respect the MHD interface, but are not needed here.
    std::ignore = cls;

    // Fire request_completed BEFORE the modded_request is
    // destroyed so the ctx pointers remain backed by live storage. The
    // gate-and-fire helper reads any_hooks_[request_completed] and
    // builds the ctx from mr->request, mr->response, and the MHD
    // termination code. The fire site is the very first thing this
    // callback does, while mr is still untouched.
    auto* mr = static_cast<detail::modded_request*>(*con_cls);
    if (mr != nullptr) {
        // mr->ws is the parent webserver -- set in answer_to_connection
        // (hoisted there). For paths where
        // answer_to_connection never ran (e.g., very early MHD failures),
        // mr->ws may be null; skip the fire site in that degenerate case.
        if (mr->ws != nullptr && mr->ws->impl_ != nullptr) {
            mr->ws->impl_->fire_request_completed_gated(mr, toe);
        }
    }

    // (1) Destroy the modded_request first. This runs ~http_request,
    //     which calls the arena_deleter on the impl's unique_ptr (a
    //     destructor-only call: monotonic_buffer_resource never
    //     deallocates per-object), running every PMR string/vector/map
    //     destructor before we reset the arena.
    delete static_cast<detail::modded_request*>(*con_cls);
    *con_cls = nullptr;

    // (2) Now that no live object inside the arena's storage remains,
    //     rewind the bump pointer AND secure-zero the initial buffer so
    //     credentials from the completed request do not linger in the
    //     reused memory (CWE-226 / CWE-14).
    //     reset_arena() does release + non-elidable zero atomically; see
    //     connection_state::reset_arena() docs and
    //     httpserver/detail/secure_zero.hpp for the platform-specific
    //     dispatch. The next request on this keep-alive
    //     connection reuses the same memory (verified by
    //     http_request_arena and connection_state_sentinel unit tests).
    //
    // Unconditional release is correct regardless of the `toe`
    // (MHD_RequestTerminationCode) value: step (1) above always destroys
    // the modded_request (and thus all arena-backed objects) before this
    // point, so the arena holds no live objects for any termination code,
    // including MHD_REQUEST_TERMINATED_WITH_ERROR. Resetting unconditionally
    // is therefore both safe and necessary to prepare the arena for the next
    // keep-alive request.
    //
    // MHD ordering guarantee: NOTIFY_COMPLETED always fires before
    // NOTIFY_CLOSED for the same connection (MHD documentation, section
    // "Thread model guarantees"). Therefore the connection_state pointer
    // accessed here is guaranteed live. The NOTIFY_CLOSED handler
    // (connection_notify) must NOT be called concurrently on a different
    // thread for the same connection while this callback is executing.
    // (Thread-safety ordering invariant.)
    if (connection != nullptr) {
        const MHD_ConnectionInfo* ci = MHD_get_connection_info(
            connection, MHD_CONNECTION_INFO_SOCKET_CONTEXT);
        if (ci != nullptr && ci->socket_context != nullptr) {
            auto* cs = static_cast<detail::connection_state*>(ci->socket_context);
            cs->reset_arena();
        }
    }
}

// connection_notify (NOTIFY_STARTED / NOTIFY_CLOSED) and policy_callback
// live in webserver_callbacks_lifecycle.cpp. Both
// callbacks fire lifecycle hooks and reach into <sys/socket.h> via the
// peer-address adapter; isolating them keeps this TU under the project
// FILE_LOC_MAX gate.

#ifdef HAVE_GNUTLS
// MHD_PskServerCredentialsCallback signature:
// The 'cls' parameter is our webserver pointer (passed via MHD_OPTION)
// Returns 0 on success, -1 on error
// The psk output should be allocated with malloc() - MHD will free it
int webserver_impl::psk_cred_handler_func(void* cls,
                                      struct MHD_Connection* connection,
                                      const char* username,
                                      void** psk,
                                      size_t* psk_size) {
    std::ignore = connection;  // Not needed - we get context from cls

    webserver* ws = static_cast<webserver*>(cls);

    // Initialize output to safe values
    *psk = nullptr;
    *psk_size = 0;

    if (ws == nullptr || ws->config.psk_cred_handler == nullptr) {
        return -1;
    }

    std::string psk_hex = ws->config.psk_cred_handler(std::string(username));
    if (psk_hex.empty()) {
        return -1;
    }

    // Validate hex string before allocating memory
    size_t psk_len = psk_hex.size() / 2;
    if (psk_len == 0 || (psk_hex.size() % 2 != 0) ||
        !string_utilities::is_valid_hex(psk_hex)) {
        return -1;
    }

    // Allocate with malloc - MHD will free this
    unsigned char* psk_data = static_cast<unsigned char*>(malloc(psk_len));
    if (psk_data == nullptr) {
        return -1;
    }

    // Convert hex string to binary
    for (size_t i = 0; i < psk_len; i++) {
        psk_data[i] = static_cast<unsigned char>(
            (string_utilities::hex_char_to_val(psk_hex[i * 2]) << 4) |
             string_utilities::hex_char_to_val(psk_hex[i * 2 + 1]));
    }

    *psk = psk_data;
    *psk_size = psk_len;
    return 0;
}

#ifdef MHD_OPTION_HTTPS_CERT_CALLBACK
// SNI callback for selecting certificates based on server name
// Returns 0 on success, -1 on failure
int webserver_impl::sni_cert_callback_func(void* cls,
                                       struct MHD_Connection* connection,
                                       const char* server_name,
                                       gnutls_certificate_credentials_t* creds) {
    std::ignore = connection;

    webserver* ws = static_cast<webserver*>(cls);
    if (ws == nullptr || ws->config.sni_callback == nullptr || server_name == nullptr) {
        return -1;
    }

    webserver_impl* impl = ws->impl_.get();

    std::string name(server_name);

    // Check if we have cached credentials for this server name
    {
        std::shared_lock lock(impl->sni_credentials_mutex);
        auto it = impl->sni_credentials_cache.find(name);
        if (it != impl->sni_credentials_cache.end()) {
            *creds = it->second;
            return 0;
        }
    }

    // Call user's callback to get cert/key pair
    auto [cert_pem, key_pem] = ws->config.sni_callback(name);
    if (cert_pem.empty() || key_pem.empty()) {
        return -1;  // Use default certificate
    }

    // Create new credentials for this server name
    gnutls_certificate_credentials_t new_creds;
    if (gnutls_certificate_allocate_credentials(&new_creds) != GNUTLS_E_SUCCESS) {
        return -1;
    }

    gnutls_datum_t cert_data = {
        reinterpret_cast<unsigned char*>(const_cast<char*>(cert_pem.data())),
        static_cast<unsigned int>(cert_pem.size())
    };
    gnutls_datum_t key_data = {
        reinterpret_cast<unsigned char*>(const_cast<char*>(key_pem.data())),
        static_cast<unsigned int>(key_pem.size())
    };

    int ret = gnutls_certificate_set_x509_key_mem(new_creds, &cert_data, &key_data, GNUTLS_X509_FMT_PEM);
    if (ret != GNUTLS_E_SUCCESS) {
        gnutls_certificate_free_credentials(new_creds);
        return -1;
    }

    // Cache the credentials with double-check to avoid race condition
    {
        std::unique_lock lock(impl->sni_credentials_mutex);
        // Re-check after acquiring exclusive lock - another thread may have inserted
        auto it = impl->sni_credentials_cache.find(name);
        if (it != impl->sni_credentials_cache.end()) {
            // Another thread already cached credentials, use theirs and free ours
            gnutls_certificate_free_credentials(new_creds);
            *creds = it->second;
            return 0;
        }
        impl->sni_credentials_cache[name] = new_creds;
    }

    *creds = new_creds;
    return 0;
}
#endif  // MHD_OPTION_HTTPS_CERT_CALLBACK
#endif  // HAVE_GNUTLS

void* webserver_impl::uri_log(void* cls, const char* uri, struct MHD_Connection *con) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = cls;
    std::ignore = con;

    auto mr = std::make_unique<detail::modded_request>();
    // MHD may invoke this callback with a null uri before the request line
    // has been parsed (e.g. port scans, half-open connections, or non-HTTP
    // traffic on the listening port). Treat that as an empty URI so the
    // std::string assignment does not throw std::logic_error and abort the
    // process via std::terminate. See issue #371.
    mr->complete_uri = (uri != nullptr) ? uri : "";
    return reinterpret_cast<void*>(mr.release());
}

void webserver_impl::error_log(void* cls, const char* fmt, va_list ap) {
    webserver* dws = static_cast<webserver*>(cls);

    std::string msg;
    msg.resize(80);  // Assume one line will be enough most of the time.

    va_list va;
    va_copy(va, ap);  // Stash a copy in case we need to try again.

    size_t r = vsnprintf(&*msg.begin(), msg.size(), fmt, ap);
    va_end(ap);

    if (msg.size() < r) {
      msg.resize(r);
      r = vsnprintf(&*msg.begin(), msg.size(), fmt, va);
    }
    va_end(va);
    msg.resize(r);

    if (dws->config.log_error != nullptr) dws->config.log_error(msg);
}

size_t webserver_impl::unescaper_func(void * cls, struct MHD_Connection *c, char *s) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = cls;
    std::ignore = c;

    // No-op unescaper: returns the input length and does not mutate `s`,
    // so MHD ships raw percent-encoded bytes to our get_connection_values
    // callbacks. Decoding is performed by libhttpserver itself:
    //   - request URL: base_unescaper() in webserver_request.cpp
    //     (answer_to_connection, line ~418)
    //   - GET args: unescape_in_arena() in http_request_impl.cpp
    // This is required so we can honour a user-registered unescaper hook
    // (create_webserver::unescaper(...)) and route GET-arg decoding through
    // the per-connection arena. Per microhttpd.h, registering a custom
    // MHD_OPTION_UNESCAPE_CALLBACK suppresses MHD's internal "%HH" decode,
    // so this no-op is what guarantees MHD does not pre-decode behind our
    // back (which would otherwise cause double-decoding of `?key=%2F`).
    //
    // Historical note (verified against upstream libmicrohttpd
    // ChangeLog): this callback originally also worked around a v0.99-era
    // MHD bug where the internal unescape could produce strings containing
    // embedded NULs (e.g. from `%00`), which then broke
    // MHD_get_connection_values / MHD_lookup_connection_value lookups
    // downstream. Upstream resolved that by adding explicit
    // binary-zero-aware key/value storage and the size-carrying
    // MHD_KeyValueIteratorN callback in libmicrohttpd 0.9.64 (released
    // 2019-06-09; see ChangeLog entries dated 2019-03-20, 2019-05-01,
    // 2019-05-03; https://git.gnunet.org/libmicrohttpd.git/log/?qt=grep&q=0.9.64).
    // configure.ac requires libmicrohttpd >= 1.0.0 (released 2024-02-01),
    // so the original v0.99 bug is no longer reachable; the no-op stays
    // for the architectural reasons above.
    if (s == nullptr) return 0;
    return std::char_traits<char>::length(s);
}

MHD_Result webserver_impl::handle_post_form_arg(detail::modded_request* mr,
        const char* key, const char* data, size_t size, uint64_t off) {
    // MHD may invoke the post iterator with a null key on a continuation
    // chunk (off > 0): the field name was supplied on the first call and
    // is not repeated. With no field name there is nothing to store the
    // value under, so silently accept the chunk (MHD_YES tells MHD to
    // continue; MHD_NO would abort the whole request). Guarding here also
    // stops the raw pointer from reaching std::string, which throws
    // std::logic_error on null and aborts the process via std::terminate
    // because the throw escapes a C callback. See issue #375 (same class
    // of bug as the null-uri fix in uri_log, issue #371).
    if (key == nullptr) {
        return MHD_YES;
    }
    // No file: set the arg key/value and return. A non-zero @p off
    // means MHD is feeding us a continuation chunk of a previously-
    // started value, so append rather than replace.
    if (off > 0) {
        mr->request->grow_last_arg(key, std::string(data, size));
    } else {
        mr->request->set_arg(key, std::string(data, size));
    }
    return MHD_YES;
}

bool webserver_impl::setup_new_upload_file_info(http::file_info& file,
        const char* filename, const char* content_type,
        const char* transfer_encoding) const {
    // First chunk for this (key, filename) pair: choose the on-disk
    // destination path (random if generate_random_filename_on_upload,
    // otherwise sanitize the client-supplied filename) and prime the
    // file_info with content_type / transfer_encoding when MHD gave
    // them to us.
    if (parent->config.generate_random_filename_on_upload) {
        file.set_file_system_file_name(
            http_utils::generate_random_upload_filename(parent->config.file_upload_dir));
    } else {
        std::string safe_name = http_utils::sanitize_upload_filename(filename);
        if (safe_name.empty()) return false;
        file.set_file_system_file_name(parent->config.file_upload_dir + "/" + safe_name);
    }
    // Avoid appending to a leftover file from a previous request.
    unlink(file.get_file_system_file_name().c_str());
    if (content_type != nullptr) file.set_content_type(content_type);
    if (transfer_encoding != nullptr) file.set_transfer_encoding(transfer_encoding);
    return true;
}

void webserver_impl::manage_upload_stream(detail::modded_request* mr,
        const char* filename, const char* key, http::file_info& file) {
    // If MHD switches us to a different (filename, key) pair, close the
    // previous output stream. The four-way OR covers fresh state (both
    // tracking strings empty) and either coordinate changing.
    if (mr->upload_filename.empty()
            || mr->upload_key.empty()
            || strcmp(filename, mr->upload_filename.c_str()) != 0
            || strcmp(key, mr->upload_key.c_str()) != 0) {
        if (mr->upload_ostrm != nullptr) mr->upload_ostrm->close();
    }
    // Open a stream when we don't already have one (first chunk, or
    // just-closed above).
    if (mr->upload_ostrm == nullptr || !mr->upload_ostrm->is_open()) {
        mr->upload_key = key;
        mr->upload_filename = filename;
        mr->upload_ostrm = std::make_unique<std::ofstream>();
        mr->upload_ostrm->open(file.get_file_system_file_name(),
                               std::ios::binary | std::ios::app);
    }
}

MHD_Result webserver_impl::process_file_upload(detail::modded_request* mr,
        const char* key, const char* filename, const char* content_type,
        const char* transfer_encoding, const char* data, size_t size) const {
    http::file_info& file = mr->request->get_or_create_file_info(key, filename);
    if (file.get_file_system_file_name().empty()) {
        if (!setup_new_upload_file_info(file, filename, content_type, transfer_encoding)) {
            return MHD_NO;
        }
    }
    manage_upload_stream(mr, filename, key, file);
    if (size > 0) {
        mr->upload_ostrm->write(data, size);
        if (!mr->upload_ostrm->good()) return MHD_NO;
    }
    file.grow_file_size(size);
    return MHD_YES;
}

MHD_Result webserver_impl::post_iterator(void *cls, enum MHD_ValueKind kind,
        const char *key, const char *filename, const char *content_type,
        const char *transfer_encoding, const char *data, uint64_t off, size_t size) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = kind;
    auto* mr = static_cast<detail::modded_request*>(cls);

    if (!filename) {
        return handle_post_form_arg(mr, key, data, size, off);
    }

    try {
        if (mr->ws->config.file_upload_target != FILE_UPLOAD_DISK_ONLY) {
            mr->request->set_arg_flat(key,
                std::string(mr->request->get_arg(key)) + std::string(data, size));
        }
        if (*filename != '\0' && mr->ws->config.file_upload_target != FILE_UPLOAD_MEMORY_ONLY) {
            MHD_Result r = mr->ws->impl_->process_file_upload(
                mr, key, filename, content_type, transfer_encoding, data, size);
            if (r != MHD_YES) return r;
        }
        return MHD_YES;
    } catch (const http::generateFilenameException&) {
        return MHD_NO;
    }
}

}  // namespace detail

}  // namespace httpserver
