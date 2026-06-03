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
	scripts/check-examples.sh scripts/check-readme.sh scripts/check-release-notes.sh \
	scripts/check-doxygen.sh scripts/check-hooks-doc-spotcheck.sh \
	scripts/check-soversion.sh scripts/check-parallel-install.sh \
	scripts/lib/resolve-prefix.sh \
	scripts/check-complexity.sh scripts/check-duplication.sh \
	scripts/check-file-size.sh \
	scripts/verify-installed-examples.sh \
	test/headers/consumer_direct.cpp test/headers/consumer_detail.cpp test/headers/consumer_detail_modded.cpp \
	test/headers/consumer_umbrella.cpp \
	test/headers/consumer_post_umbrella.cpp \
	test/headers/consumer_umbrella_no_backend.cpp

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
# The CXX invocations below omit CXXFLAGS, AM_CPPFLAGS, and per-target CPPFLAGS
# so that -DHTTPSERVER_COMPILATION (set per-directory in src/Makefile.am and
# test/Makefile.am AM_CPPFLAGS, not in configure.ac global CXXFLAGS) does NOT
# leak into the consumer-style compile. We still pass -std=c++20 explicitly
# because libhttpserver requires C++20.
# ---------------------------------------------------------------------------

# Compose CXX with: explicit -std, the source/build include search paths used by
# the library, and $(CPPFLAGS) (e.g., -I/opt/homebrew/include from configure).
# Deliberately omit $(CXXFLAGS), $(AM_CPPFLAGS), and any per-target CPPFLAGS so
# that -DHTTPSERVER_COMPILATION (set in src/ and test/ AM_CPPFLAGS) cannot
# leak into the consumer-style compile. A true consumer never has that macro.
CHECK_HEADERS_CXX = $(CXX) -std=c++20 -I$(top_builddir) -I$(top_srcdir)/src -I$(top_srcdir)/src/httpserver $(CPPFLAGS)
# Public-header gate (consumer_direct, consumer_post_umbrella): triggered when
# a public httpserver/* header is included without the umbrella having defined
# _HTTPSERVER_HPP_INSIDE_.
CHECK_HEADERS_PUBLIC_GATE_MSG = Only <httpserver.hpp> or <httpserverpp> can be included directly.
# Detail-header gate (consumer_detail, consumer_detail_modded): post-TASK-014
# the detail-header gate is HTTPSERVER_COMPILATION-only and emits a message of
# the form "httpserver/detail/<file>.hpp is internal; only include it when
# compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
CHECK_HEADERS_DETAIL_GATE_MSG = is internal; only include it when compiling libhttpserver

check-headers:
	@echo "=== check-headers A.1: direct public-header include must fail ==="
	@if $(CHECK_HEADERS_CXX) -c $(top_srcdir)/test/headers/consumer_direct.cpp -o /dev/null 2>check-headers-A1.log; then \
	    echo "FAIL: consumer_direct.cpp compiled but should have errored"; \
	    cat check-headers-A1.log; \
	    rm -f check-headers-A1.log; \
	    exit 1; \
	fi
	@if ! grep -q "$(CHECK_HEADERS_PUBLIC_GATE_MSG)" check-headers-A1.log; then \
	    echo "FAIL: consumer_direct.cpp failed but not for the gate reason"; \
	    cat check-headers-A1.log; \
	    rm -f check-headers-A1.log; \
	    exit 1; \
	fi
	@rm -f check-headers-A1.log
	@echo "  PASS: A.1 gate fired as expected"
	@echo "=== check-headers A.2: direct detail-header include must fail ==="
	@# Post-TASK-014: the detail-header gate is HTTPSERVER_COMPILATION-only.
	@# consumer_detail.cpp defines _HTTPSERVER_HPP_INSIDE_ explicitly so this
	@# check validates the stricter scenario (the macro alone must not satisfy
	@# the gate). A.2 is distinct from A.1 because of the include-path
	@# (detail/ vs public) and the different gate message.
	@if $(CHECK_HEADERS_CXX) -c $(top_srcdir)/test/headers/consumer_detail.cpp -o /dev/null 2>check-headers-A2.log; then \
	    echo "FAIL: consumer_detail.cpp compiled but should have errored"; \
	    cat check-headers-A2.log; \
	    rm -f check-headers-A2.log; \
	    exit 1; \
	fi
	@if ! grep -q "$(CHECK_HEADERS_DETAIL_GATE_MSG)" check-headers-A2.log; then \
	    echo "FAIL: consumer_detail.cpp failed but not for the gate reason"; \
	    cat check-headers-A2.log; \
	    rm -f check-headers-A2.log; \
	    exit 1; \
	fi
	@rm -f check-headers-A2.log
	@echo "  PASS: A.2 gate fired as expected"
	@echo "=== check-headers A.2b: direct detail-header (modded_request) include must fail ==="
	@# Complements A.2 (http_endpoint.hpp); both detail headers must carry the gate.
	@if $(CHECK_HEADERS_CXX) -c $(top_srcdir)/test/headers/consumer_detail_modded.cpp -o /dev/null 2>check-headers-A2b.log; then \
	    echo "FAIL: consumer_detail_modded.cpp compiled but should have errored"; \
	    cat check-headers-A2b.log; \
	    rm -f check-headers-A2b.log; \
	    exit 1; \
	fi
	@if ! grep -q "$(CHECK_HEADERS_DETAIL_GATE_MSG)" check-headers-A2b.log; then \
	    echo "FAIL: consumer_detail_modded.cpp failed but not for the gate reason"; \
	    cat check-headers-A2b.log; \
	    rm -f check-headers-A2b.log; \
	    exit 1; \
	fi
	@rm -f check-headers-A2b.log
	@echo "  PASS: A.2b gate fired as expected (modded_request.hpp)"
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
	@if ! grep -q "$(CHECK_HEADERS_PUBLIC_GATE_MSG)" check-headers-A4.log; then \
	    echo "FAIL: consumer_post_umbrella.cpp failed but not for the gate reason"; \
	    cat check-headers-A4.log; \
	    rm -f check-headers-A4.log; \
	    exit 1; \
	fi
	@rm -f check-headers-A4.log
	@echo "  PASS: A.4 umbrella does not leak _HTTPSERVER_HPP_INSIDE_"

# check-install-layout asserts that `make install DESTDIR=$(STAGE)` produces
# a public include tree with NO `detail/` directory and NO `*_impl.hpp` files,
# and that the httpserverpp symlink is present.
# This protects the public/private split as described in TASK-002 / DR-002.
#
# NOTE: when invoked standalone (without CHECK_INSTALL_SHARED=yes), this target
# runs `make install` internally, which requires the library to have been built
# first. If the library has not been compiled yet, the staged install will fail
# with a confusing error. Always run `make` before `make check-install-layout`
# when invoking standalone outside of `make check`.
CHECK_INSTALL_STAGE = $(abs_top_builddir)/.install-stage

check-install-layout:
	@echo "=== check-install-layout: staged install must hide detail/ and *_impl.hpp ==="
	@if test "$(CHECK_INSTALL_SHARED)" != "yes"; then \
	    rm -rf $(CHECK_INSTALL_STAGE); \
	    mkdir -p $(CHECK_INSTALL_STAGE); \
	    $(MAKE) $(AM_MAKEFLAGS) install DESTDIR=$(CHECK_INSTALL_STAGE) >$(CHECK_INSTALL_STAGE)/install.log 2>&1 || { \
	        echo "FAIL: staged install failed"; \
	        cat $(CHECK_INSTALL_STAGE)/install.log; \
	        rm -rf $(CHECK_INSTALL_STAGE); \
	        exit 1; \
	    }; \
	fi
	@leaked_detail=`find $(CHECK_INSTALL_STAGE) -type d -name detail 2>/dev/null`; \
	if test -n "$$leaked_detail"; then \
	    echo "FAIL: detail/ directory leaked into install:"; \
	    echo "$$leaked_detail"; \
	    if test "$(CHECK_INSTALL_SHARED)" != "yes"; then rm -rf $(CHECK_INSTALL_STAGE); fi; \
	    exit 1; \
	fi
	@# '*_impl.hpp' is the agreed suffix for PIMPL implementation headers (DR-002).
	@# Any such file appearing in the installed tree indicates a Makefile regression.
	@leaked_impl=`find $(CHECK_INSTALL_STAGE) -name '*_impl.hpp' 2>/dev/null`; \
	if test -n "$$leaked_impl"; then \
	    echo "FAIL: *_impl.hpp file leaked into install:"; \
	    echo "$$leaked_impl"; \
	    if test "$(CHECK_INSTALL_SHARED)" != "yes"; then rm -rf $(CHECK_INSTALL_STAGE); fi; \
	    exit 1; \
	fi
	@umbrella_count=`find $(CHECK_INSTALL_STAGE) -name 'httpserver.hpp' | wc -l | tr -d ' '`; \
	if test "$$umbrella_count" != "1"; then \
	    echo "FAIL: expected exactly 1 installed httpserver.hpp, got $$umbrella_count"; \
	    if test "$(CHECK_INSTALL_SHARED)" != "yes"; then rm -rf $(CHECK_INSTALL_STAGE); fi; \
	    exit 1; \
	fi
	@if ! test -L $(CHECK_INSTALL_STAGE)$(includedir)/httpserverpp; then \
	    echo "FAIL: httpserverpp symlink not installed under $(includedir)"; \
	    if test "$(CHECK_INSTALL_SHARED)" != "yes"; then rm -rf $(CHECK_INSTALL_STAGE); fi; \
	    exit 1; \
	fi
	@if test "$(CHECK_INSTALL_SHARED)" != "yes"; then rm -rf $(CHECK_INSTALL_STAGE); fi
	@echo "  PASS: staged install layout is clean"

# ---------------------------------------------------------------------------
# Header-hygiene preprocessor gate (TASK-007).
#
# This is the preprocessor-grep half of the TASK-007 enforcement (the
# compile-time half lives as `header_hygiene` in test/Makefile.am).
#
# Procedure:
#   1. Stage `make install DESTDIR=$(CHECK_HYGIENE_STAGE)` to get a
#      pristine public include tree -- exactly what packagers and
#      downstream consumers see.
#   2. Preprocess test/headers/consumer_umbrella_no_backend.cpp using
#      ONLY -I$(CHECK_HYGIENE_STAGE)$(includedir) plus $(CPPFLAGS) (so
#      e.g. /opt/homebrew/include is on the search path -- the grep
#      below NEEDS to resolve <microhttpd.h> if the umbrella pulls it
#      in, otherwise we couldn't detect the leak).
#   3. Grep the cpp output for `# <line> "<file>"` line markers that
#      name any forbidden backend header. The line-marker filter
#      avoids false positives from substrings in code or comments.
#
# HEADER_HYGIENE_STRICT controls whether a leak is fatal:
#   - "yes" (default since TASK-020): leaks fail the build. The umbrella
#           is now clean and any regression should break CI loudly.
#   - "no"  (legacy): leaks were reported as EXPECTED-FAIL and exit 0
#           while M2-M5 were in flight. Override from the command line
#           (`make check-hygiene HEADER_HYGIENE_STRICT=no`) only if you
#           are deliberately running against an in-flight umbrella.
#
# Cross-reference: keep HEADER_HYGIENE_FORBIDDEN in sync with the
# #ifdef ladder in test/unit/header_hygiene_test.cpp.
#
# TASK-020 caveat (libc++ AND libstdc++ in thread mode): <pthread.h>
# is intentionally absent from the forbidden list below. Both
# mainstream STLs unconditionally pull <pthread.h> in from any STL
# container header (<vector>, <string>, <map>, etc.) when threading
# is enabled:
#   - libc++ (Apple's default STL on macOS) routes through
#     <__thread/support/pthread.h>.
#   - libstdc++ in thread-enabled mode (which is the default whenever
#     -D_REENTRANT is set, as configure.ac does) routes through
#     <bits/gthr-default.h>, which #include <pthread.h> directly.
# The resulting `# N "...pthread.h"` line markers therefore appear in
# the preprocessed output even though libhttpserver itself does not
# include <pthread.h>. The runtime sentinel
# test/unit/header_hygiene_test.cpp keeps the pthread guards but skips
# them on both libc++ (_LIBCPP_VERSION) and libstdc++ in thread mode
# (_GLIBCXX_HAS_GTHREADS), so the guards still fire on STLs that don't
# route std::thread through pthread (e.g. MSVC's Microsoft STL).
# pthread.h is intentionally absent from HEADER_HYGIENE_FORBIDDEN below
# for the same reason: the grep-based check would produce false positives
# on libc++ and libstdc++ in thread mode. The runtime sentinel in
# test/unit/header_hygiene_test.cpp enforces the pthread invariant via
# the #ifndef _LIBCPP_VERSION && !_GLIBCXX_HAS_GTHREADS guard ladder so
# it still fires on STLs that do not route std::thread through pthread.
# ---------------------------------------------------------------------------

# NOTE: each entry matches a basename; the grep in check-hygiene anchors with a
# leading '/' so e.g. mypthread.h is not a false positive.  Keep this list in
# sync with the #ifdef ladder in test/unit/header_hygiene_test.cpp.
HEADER_HYGIENE_FORBIDDEN = microhttpd\.h|gnutls/gnutls\.h|sys/socket\.h|sys/uio\.h
CHECK_HYGIENE_STAGE = $(abs_top_builddir)/.hygiene-stage
CHECK_HYGIENE_CXX = $(CXX) -std=c++20 -E -I$(CHECK_HYGIENE_STAGE)$(includedir) $(CPPFLAGS)
# Override on the command line to run against an in-flight umbrella:
#   make check-hygiene HEADER_HYGIENE_STRICT=no
HEADER_HYGIENE_STRICT ?= yes

# Sentinel file: only re-run the staged install when headers have changed.
# This is an mtime gate used exclusively for standalone `make check-hygiene`
# invocations — it avoids paying a full `make install` cost on every
# repeated standalone run.  When check-local drives check-hygiene it sets
# CHECK_HYGIENE_SHARED=yes and passes CHECK_HYGIENE_STAGE pointing at its
# own pre-built shared stage, so this stamp target is bypassed entirely.
HYGIENE_STAMP = $(CHECK_HYGIENE_STAGE)/.hygiene-stamp

# NOTE: wildcard is evaluated at parse time; run `make clean` if new headers
# are added to src/httpserver/ in the same invocation that generates them.
# Makefile.am and consumer_umbrella_no_backend.cpp are also listed so that
# changes to HEADER_HYGIENE_FORBIDDEN or the consumer TU invalidate the stamp.
$(HYGIENE_STAMP): $(wildcard $(top_srcdir)/src/httpserver/*.hpp) \
                  $(top_srcdir)/src/httpserver.hpp \
                  $(top_srcdir)/Makefile.am \
                  $(top_srcdir)/test/headers/consumer_umbrella_no_backend.cpp
	@rm -rf $(CHECK_HYGIENE_STAGE)
	@$(MAKE) $(AM_MAKEFLAGS) install DESTDIR=$(CHECK_HYGIENE_STAGE) >check-hygiene-install.log 2>&1 || { \
	    echo "FAIL: staged install failed"; cat check-hygiene-install.log; \
	    rm -f check-hygiene-install.log; rm -rf $(CHECK_HYGIENE_STAGE); exit 1; }
	@rm -f check-hygiene-install.log
	@touch $(HYGIENE_STAMP)

check-hygiene:
	@echo "=== check-hygiene: <httpserver.hpp> must not transitively include backend headers ==="
	@if test "$(CHECK_HYGIENE_SHARED)" != "yes"; then \
	    $(MAKE) $(AM_MAKEFLAGS) $(HYGIENE_STAMP); \
	else \
	    if ! test -d "$(CHECK_HYGIENE_STAGE)"; then \
	        echo "FAIL: CHECK_HYGIENE_SHARED=yes but stage dir '$(CHECK_HYGIENE_STAGE)' does not exist."; \
	        echo "  Always pair CHECK_HYGIENE_SHARED=yes with CHECK_HYGIENE_STAGE=<valid-dir>."; \
	        exit 1; \
	    fi; \
	    if ! test -f "$(CHECK_HYGIENE_STAGE)$(includedir)/httpserver.hpp"; then \
	        echo "FAIL: CHECK_HYGIENE_STAGE exists but does not contain $(includedir)/httpserver.hpp"; \
	        echo "  Was the staged install complete before invoking check-hygiene?"; \
	        exit 1; \
	    fi; \
	fi
	@status=0; \
	if ! $(CHECK_HYGIENE_CXX) $(top_srcdir)/test/headers/consumer_umbrella_no_backend.cpp >check-hygiene.i 2>check-hygiene.err; then \
	    if test "$(HEADER_HYGIENE_STRICT)" = "yes"; then \
	        echo "FAIL: preprocessor failed"; cat check-hygiene.err; \
	        status=1; \
	    else \
	        echo "EXPECTED-FAIL (informational until M5): preprocessor failed against staged install."; \
	        echo "  This is expected while M2-M5 are in flight (e.g. webserver.hpp still"; \
	        echo "  references private detail headers that aren't shipped)."; \
	        echo "  Tail of preprocessor diagnostics:"; \
	        sed 's/^/    /' check-hygiene.err | tail -10; \
	    fi; \
	else \
	    leaks=`grep -hE '^# [0-9]+ "[^"]*/($(HEADER_HYGIENE_FORBIDDEN))"' check-hygiene.i | sed 's/.*"\(.*\)".*/\1/' | sort -u`; \
	    if test -n "$$leaks"; then \
	        if test "$(HEADER_HYGIENE_STRICT)" = "yes"; then \
	            echo "FAIL: forbidden headers leaked through <httpserver.hpp>:"; \
	            echo "$$leaks"; \
	            status=1; \
	        else \
	            echo "EXPECTED-FAIL (informational until M5): forbidden headers currently leak through <httpserver.hpp>:"; \
	            echo "$$leaks"; \
	        fi; \
	    else \
	        echo "  PASS: no forbidden headers reached the consumer TU"; \
	    fi; \
	fi; \
	rm -f check-hygiene.i check-hygiene.err; \
	exit $$status

# check-local runs the following static (no-install) checks first:
#   check-headers, check-examples, check-readme, check-release-notes,
#   check-doxygen, check-hooks-doc-spotcheck
# Then runs check-install-layout and check-hygiene against a single shared
# staged install to avoid paying two full `make install` costs on every
# `make check`. Both install-backed sub-checks can still be invoked standalone
# (they will do their own install when CHECK_*_SHARED is not set).
#
# CHECK_INSTALL_SHARED=yes / CHECK_HYGIENE_SHARED=yes: the caller has already
# staged the install; sub-check skips the install step to avoid double cost.
# This knob is set only by check-local; do not set it when invoking sub-checks
# standalone.
check-local: check-headers check-examples check-readme check-release-notes check-doxygen check-hooks-doc-spotcheck
	@echo "=== Shared staged install for check-install-layout and check-hygiene ==="
	@rm -rf "$(abs_top_builddir)/.shared-check-stage"
	@$(MAKE) $(AM_MAKEFLAGS) install DESTDIR="$(abs_top_builddir)/.shared-check-stage" >check-shared-install.log 2>&1 || { \
	    echo "FAIL: shared staged install failed"; cat check-shared-install.log; \
	    rm -f check-shared-install.log; rm -rf "$(abs_top_builddir)/.shared-check-stage"; exit 1; }
	@rm -f check-shared-install.log
	@$(MAKE) $(AM_MAKEFLAGS) check-install-layout \
	    CHECK_INSTALL_STAGE="$(abs_top_builddir)/.shared-check-stage" \
	    CHECK_INSTALL_SHARED=yes
	@$(MAKE) $(AM_MAKEFLAGS) check-hygiene \
	    CHECK_HYGIENE_STAGE="$(abs_top_builddir)/.shared-check-stage" \
	    CHECK_HYGIENE_SHARED=yes
	@rm -rf "$(abs_top_builddir)/.shared-check-stage"

# TASK-040: enforce static invariants on the two flagship examples
# (hello_world.cpp <= 10 LOC, lambda-only; shared_state.cpp class+mutex).
# Run as part of `make check` via check-local.
check-examples:
	@echo "=== check-examples: enforce TASK-040 invariants on examples/ ==="
	@$(top_srcdir)/scripts/check-examples.sh

# TASK-041: enforce static invariants on README.md
# (byte-for-byte hello-world snippet, no v1 tokens, required sections, cross-links).
# Run as part of `make check` via check-local.
check-readme:
	@echo "=== check-readme: enforce TASK-041 invariants on README.md ==="
	@$(top_srcdir)/scripts/check-readme.sh

# TASK-042: static checks on RELEASE_NOTES.md — see scripts/check-release-notes.sh
check-release-notes:
	@echo "=== check-release-notes: enforce TASK-042 invariants on RELEASE_NOTES.md ==="
	@$(top_srcdir)/scripts/check-release-notes.sh

# TASK-043: run doxygen and fail on any substantive warning. The script
# delegates to `make doxygen-run` and parses stderr; environmental noise
# (obsolete config tags, missing graphviz) is filtered out. Skips with
# exit 0 if doxygen is not installed.
check-doxygen:
	@echo "=== check-doxygen: enforce TASK-043 zero-warning invariant ==="
	@BUILD_DIR="$(abs_top_builddir)" $(top_srcdir)/scripts/check-doxygen.sh

# TASK-052: hook-bus documentation spotcheck (per-header @file/@brief blocks,
# eleven-phase enumeration on webserver::add_hook, five-permitted /
# six-rejected enumeration on http_resource::add_hook, alias callouts on the
# five v1 setters, hook-concurrency sentence in the class-level threading
# block). Static grep-based gate; complements check-doxygen's full Doxygen
# warning sweep.
check-hooks-doc-spotcheck:
	@echo "=== check-hooks-doc-spotcheck: enforce TASK-052 invariants on hook public surface ==="
	@$(top_srcdir)/scripts/check-hooks-doc-spotcheck.sh

# TASK-044: SOVERSION acceptance gate. Stages a clean DESTDIR install and
# asserts that the v2.0 on-disk SONAME layout is correct (libhttpserver.so.2
# on Linux / libhttpserver.2.dylib on Darwin, pkg-config --modversion 2.0.0,
# correct symlink chain). NOT wired into `make check` — it does its own
# `make install` and is intentionally invoked standalone to keep per-PR CI
# fast. Run manually before tagging a release.
check-soversion:
	@echo "=== check-soversion: TASK-044 SOVERSION acceptance gate ==="
	@BUILD_DIR="$(abs_top_builddir)" $(top_srcdir)/scripts/check-soversion.sh

# TASK-044: parallel-installability verification. Builds master and installs
# v1 + v2 into the same DESTDIR and asserts both SONAMEd libraries coexist.
# Best-effort: emits SKIP and exits 0 if the v1 source cannot be built in
# this environment (toolchain mismatch, missing libtoolize, etc.).
# Definitely NOT in `make check` — requires a clean v1 build of master.
check-parallel-install:
	@echo "=== check-parallel-install: TASK-044 v1+v2 coexistence ==="
	@BUILD_DIR="$(abs_top_builddir)" $(top_srcdir)/scripts/check-parallel-install.sh

# Per-function cyclomatic complexity gate. Wraps `lizard` against src/ and
# fails if any function exceeds CCN_MAX (default in the script; ratcheted
# down as the v2.0 refactor lands each offender below the line). Not wired
# into `make check` — invoked directly by CI's `lint` lane to keep the
# gate visible as its own status.
lint-complexity:
	@echo "=== lint-complexity: enforce per-function CCN ceiling ==="
	@$(top_srcdir)/scripts/check-complexity.sh

# Copy/paste detection gate. Wraps PMD's CPD with the C++ tokenizer
# against src/ and fails if any duplicate run is at or above
# CPD_MIN_TOKENS (default 100 in the script). Not wired into `make
# check` — invoked directly by CI's `lint` lane.
lint-duplication:
	@echo "=== lint-duplication: enforce zero copy/paste duplication ==="
	@$(top_srcdir)/scripts/check-duplication.sh

# Per-file line-count gate. Fails if any src/ .cpp/.hpp exceeds
# FILE_LOC_MAX (default in the script; ratcheted down toward the
# long-term 500-line target as each offender is decomposed). Not wired
# into `make check` — invoked directly by CI's `lint` lane.
lint-file-size:
	@echo "=== lint-file-size: enforce per-file line-count ceiling ==="
	@$(top_srcdir)/scripts/check-file-size.sh

.PHONY: check-headers check-install-layout check-hygiene check-local check-examples check-readme check-release-notes check-doxygen check-hooks-doc-spotcheck check-soversion check-parallel-install lint-complexity lint-duplication lint-file-size

# TASK-039: top-level convenience rule that descends into test/ to
# build and run the bench binaries (see test/Makefile.am and
# test/PERFORMANCE.md). NOT wired into `make check`; release-mode
# only.
bench:
	$(MAKE) -C test bench

.PHONY: bench

MOSTLYCLEANFILES = $(DX_CLEANFILES) *.gcda *.gcno *.gcov \
	check-headers-A1.log check-headers-A2.log check-headers-A2b.log \
	check-headers-A3.log check-headers-A4.log \
	consumer_umbrella.check.o \
	check-hygiene.i check-hygiene.err check-hygiene-install.log \
	check-shared-install.log
DISTCLEANFILES = DIST_REVISION

clean-local:
	rm -rf "$(CHECK_HYGIENE_STAGE)" "$(abs_top_builddir)/.shared-check-stage" "$(CHECK_INSTALL_STAGE)"

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
