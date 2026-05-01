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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_FEATURE_UNAVAILABLE_HPP_
#define SRC_HTTPSERVER_FEATURE_UNAVAILABLE_HPP_

#include <stdexcept>
#include <string>
#include <string_view>

namespace httpserver {

// Exception thrown when a build-time-disabled feature is invoked at runtime.
// The class is unconditionally available regardless of HAVE_* flags so that
// downstream code can always write
//     try { ... } catch (const httpserver::feature_unavailable&) { ... }
// even in builds that compiled out the optional feature in question.
//
// The class is header-only (and inline) on purpose: it has no library
// dependencies, must be cheap to throw from anywhere in the codebase, and
// avoids ABI churn for what is effectively a labelled std::runtime_error.
class feature_unavailable : public std::runtime_error {
 public:
    feature_unavailable(std::string_view feature, std::string_view build_flag)
        : std::runtime_error(compose_message(feature, build_flag)) {}

 private:
    static std::string compose_message(std::string_view feature,
                                       std::string_view build_flag) {
        std::string msg;
        msg.reserve(feature.size() + build_flag.size() + 32);
        msg.append("feature '");
        msg.append(feature);
        msg.append("' unavailable: built without ");
        msg.append(build_flag);
        return msg;
    }
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_FEATURE_UNAVAILABLE_HPP_
