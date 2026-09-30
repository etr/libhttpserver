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
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

#ifndef SRC_HTTPSERVER_SERVER_CONFIGURATION_HPP_
#define SRC_HTTPSERVER_SERVER_CONFIGURATION_HPP_

// Umbrella for the v3 server configuration area (architecture §3.1,
// §3.4): the validated server options and their pre-listen gate, the
// hierarchical resource budgets with RAII reservations, and the
// budget-bounded route registration table. All headers are
// self-contained and compile identically with every build/TLS
// configuration; the surface is backend-neutral by construction
// (PRD-V3N-REQ-014, DR-V3-001, DR-V3-002).

#include <httpserver/server/budgets.hpp>
#include <httpserver/server/options.hpp>
#include <httpserver/server/routes.hpp>

#endif  // SRC_HTTPSERVER_SERVER_CONFIGURATION_HPP_
