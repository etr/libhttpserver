#
#     This file is part of libhttpserver
#     Copyright (C) 2011, 2012, 2013, 2014, 2015 Sebastiano Merlino
#
#     This library is free software; you can redistribute it and/or
#     modify it under the terms of the GNU Lesser General Public
#     License as published by the Free Software Foundation; either
#     version 2.1 of the License, or (at your option) any later version.
#
#     This library is distributed in the hope that it will be useful,
#     but WITHOUT ANY WARRANTY; without even the implied warranty of
#     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#     Lesser General Public License for more details.
#
#     You should have received a copy of the GNU Lesser General Public
#     License along with this library; if not, write to the Free Software
#     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

# not a GNU package. You can remove this line, if
# have all needed files, that a GNU package needs

LIBTOOL_DEPS = @LIBTOOL_DEPS@

AUTOMAKE_OPTIONS = foreign 1.4
ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src
DIST_SUBDIRS = src

if !COND_CROSS_COMPILE
SUBDIRS += test
DIST_SUBDIRS += test

if BUILD_EXAMPLES
SUBDIRS += examples
DIST_SUBDIRS += examples
endif

endif

EXTRA_DIST = libhttpserver.pc.in $(DX_CONFIG) scripts/extract-release-notes.sh scripts/validate-version.sh \
	test/headers/consumer_direct.cpp test/headers/consumer_detail.cpp test/headers/consumer_umbrella.cpp \
	test/headers/consumer_post_umbrella.cpp

# ---------------------------------------------------------------------------
# Header-hygiene checks (TASK-002)
#
# check-headers verifies that the public/private header gates are wired up
# correctly:
#   A.1 a consumer including a public header WITHOUT the umbrella must hit the
#       inclusion-gate #error.
#   A.2 a consumer including a detail header WITHOUT HTTPSERVER_COMPILATION
#       must hit the gate.
#   A.3 a consumer including only the umbrella, WITHOUT HTTPSERVER_COMPILATION,
#       must compile cleanly.
#
# The CXX invocations below override CXXFLAGS to '' so that
# -DHTTPSERVER_COMPILATION (injected by configure.ac into CXXFLAGS for the
# library and test build) does NOT leak into the consumer-style compile. We
# still pass -std=c++20 explicitly because libhttpserver requires C++20.
# ---------------------------------------------------------------------------

# Compose CXX with: explicit -std, the source/build include search paths used by
# the library, and $(CPPFLAGS) (e.g., -I/opt/homebrew/include from configure).
# Deliberately omit $(CXXFLAGS), $(AM_CPPFLAGS), and any per-target CPPFLAGS so
# that -DHTTPSERVER_COMPILATION (set in src/ and test/ AM_CPPFLAGS) cannot
# leak into the consumer-style compile. A true consumer never has that macro.
CHECK_HEADERS_CXX = $(CXX) -std=c++20 -I$(top_builddir) -I$(top_srcdir)/src -I$(top_srcdir)/src/httpserver $(CPPFLAGS)
CHECK_HEADERS_GATE_MSG = Only <httpserver.hpp> or <httpserverpp> can be included directly

check-headers:
	@echo "=== check-headers A.1: direct public-header include must fail ==="
	@if $(CHECK_HEADERS_CXX) -c $(top_srcdir)/test/headers/consumer_direct.cpp -o /dev/null 2>check-headers-A1.log; then \
	    echo "FAIL: consumer_direct.cpp compiled but should have errored"; \
	    cat check-headers-A1.log; \
	    rm -f check-headers-A1.log; \
	    exit 1; \
	fi
	@if ! grep -q "$(CHECK_HEADERS_GATE_MSG)" check-headers-A1.log; then \
	    echo "FAIL: consumer_direct.cpp failed but not for the gate reason"; \
	    cat check-headers-A1.log; \
	    rm -f check-headers-A1.log; \
	    exit 1; \
	fi
	@rm -f check-headers-A1.log
	@echo "  PASS: A.1 gate fired as expected"
	@echo "=== check-headers A.2: direct detail-header include must fail ==="
	@if $(CHECK_HEADERS_CXX) -c $(top_srcdir)/test/headers/consumer_detail.cpp -o /dev/null 2>check-headers-A2.log; then \
	    echo "FAIL: consumer_detail.cpp compiled but should have errored"; \
	    cat check-headers-A2.log; \
	    rm -f check-headers-A2.log; \
	    exit 1; \
	fi
	@if ! grep -q "$(CHECK_HEADERS_GATE_MSG)" check-headers-A2.log; then \
	    echo "FAIL: consumer_detail.cpp failed but not for the gate reason"; \
	    cat check-headers-A2.log; \
	    rm -f check-headers-A2.log; \
	    exit 1; \
	fi
	@rm -f check-headers-A2.log
	@echo "  PASS: A.2 gate fired as expected"
	@echo "=== check-headers A.3: umbrella include must succeed ==="
	@if ! $(CHECK_HEADERS_CXX) -c $(top_srcdir)/test/headers/consumer_umbrella.cpp -o consumer_umbrella.check.o 2>check-headers-A3.log; then \
	    echo "FAIL: consumer_umbrella.cpp did not compile"; \
	    cat check-headers-A3.log; \
	    rm -f check-headers-A3.log consumer_umbrella.check.o; \
	    exit 1; \
	fi
	@rm -f check-headers-A3.log consumer_umbrella.check.o
	@echo "  PASS: A.3 umbrella compiled cleanly"
	@echo "=== check-headers A.4: post-umbrella direct include must fail ==="
	@if $(CHECK_HEADERS_CXX) -c $(top_srcdir)/test/headers/consumer_post_umbrella.cpp -o /dev/null 2>check-headers-A4.log; then \
	    echo "FAIL: consumer_post_umbrella.cpp compiled but should have errored"; \
	    cat check-headers-A4.log; \
	    rm -f check-headers-A4.log; \
	    exit 1; \
	fi
	@if ! grep -q "$(CHECK_HEADERS_GATE_MSG)" check-headers-A4.log; then \
	    echo "FAIL: consumer_post_umbrella.cpp failed but not for the gate reason"; \
	    cat check-headers-A4.log; \
	    rm -f check-headers-A4.log; \
	    exit 1; \
	fi
	@rm -f check-headers-A4.log
	@echo "  PASS: A.4 umbrella does not leak _HTTPSERVER_HPP_INSIDE_"

# check-install-layout asserts that `make install DESTDIR=$(STAGE)` produces
# a public include tree with NO `details/` directory and NO `*_impl.hpp` files.
# This protects the public/private split as described in TASK-002 / DR-002.
CHECK_INSTALL_STAGE = $(abs_top_builddir)/.install-stage

check-install-layout:
	@echo "=== check-install-layout: staged install must hide details/ and *_impl.hpp ==="
	@rm -rf $(CHECK_INSTALL_STAGE)
	@$(MAKE) $(AM_MAKEFLAGS) install DESTDIR=$(CHECK_INSTALL_STAGE) >check-install.log 2>&1 || { \
	    echo "FAIL: staged install failed"; \
	    cat check-install.log; \
	    rm -f check-install.log; \
	    rm -rf $(CHECK_INSTALL_STAGE); \
	    exit 1; \
	}
	@rm -f check-install.log
	@leaked_details=`find $(CHECK_INSTALL_STAGE) -type d -name details 2>/dev/null`; \
	if test -n "$$leaked_details"; then \
	    echo "FAIL: details/ directory leaked into install:"; \
	    echo "$$leaked_details"; \
	    rm -rf $(CHECK_INSTALL_STAGE); \
	    exit 1; \
	fi
	@leaked_impl=`find $(CHECK_INSTALL_STAGE) -name '*_impl.hpp' 2>/dev/null`; \
	if test -n "$$leaked_impl"; then \
	    echo "FAIL: *_impl.hpp file leaked into install:"; \
	    echo "$$leaked_impl"; \
	    rm -rf $(CHECK_INSTALL_STAGE); \
	    exit 1; \
	fi
	@umbrella_count=`find $(CHECK_INSTALL_STAGE) -name 'httpserver.hpp' | wc -l | tr -d ' '`; \
	if test "$$umbrella_count" != "1"; then \
	    echo "FAIL: expected exactly 1 installed httpserver.hpp, got $$umbrella_count"; \
	    rm -rf $(CHECK_INSTALL_STAGE); \
	    exit 1; \
	fi
	@rm -rf $(CHECK_INSTALL_STAGE)
	@echo "  PASS: staged install layout is clean"

check-local: check-headers check-install-layout

.PHONY: check-headers check-install-layout

MOSTLYCLEANFILES = $(DX_CLEANFILES) *.gcda *.gcno *.gcov
DISTCLEANFILES = DIST_REVISION

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libhttpserver.pc

cmakemoduledir = $(datadir)/cmake/Modules
cmakemodule_DATA = cmakemodule/FindLibHttpServer.cmake

include $(top_srcdir)/aminclude.am

# Update libtool, if needed.
libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck

dist-hook:
	date >DIST_REVISION
	git branch -vv >>DIST_REVISION
	cp DIST_REVISION $(distdir)/
