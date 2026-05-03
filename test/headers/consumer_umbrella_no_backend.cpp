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

// TASK-007: consumer source used by the `make check-hygiene` target.
//
// The top-level Makefile.am preprocesses this file against ONLY the
// staged install include path (DESTDIR=$(CHECK_HYGIENE_STAGE)) plus the
// system $(CPPFLAGS), then greps the cpp output for `# <line> "..."`
// markers that name forbidden backend headers. If any appear, the
// umbrella has transitively pulled them in.
//
// We deliberately include NO standard-library headers here. Even
// <cstdio> can pull in libc internals that on some platforms touch
// <sys/uio.h>, which would produce false positives for the grep that
// is checking <httpserver.hpp> hygiene specifically.

#include <httpserver.hpp>

int main() { return 0; }
