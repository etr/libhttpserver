/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

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

// Negative test (Check A.2): a consumer including a detail header directly
// must hit the gate. The gate is HTTPSERVER_COMPILATION-only,
// so even defining _HTTPSERVER_HPP_INSIDE_ (the public-header inside-macro)
// must NOT satisfy it -- the macro is set here precisely to validate that
// the stricter gate rejects it.
#define _HTTPSERVER_HPP_INSIDE_
#include "httpserver/detail/http_endpoint.hpp"
int main() { return 0; }
