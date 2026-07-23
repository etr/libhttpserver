## 8) Build and packaging

**Compiler floor:** C++20.
- Debian 13 (trixie) GCC 14.2: full support out of the box.
- RHEL 9 stock GCC 11: requires `gcc-toolset-14` or newer (Red Hat-supported overlay; documented as the supported path).
- RHEL 10 stock GCC 14: full support.
- FreeBSD 14.x base Clang 18+: full support.
- macOS Homebrew GCC 15+ / current Apple Clang: full support.
- vcpkg / Conan baseline: GCC 13+ / Clang 16+.

**C++23 features used internally only:** `std::print`, `std::expected` (when available) may appear in `.cpp` files behind feature-test macros, never in installed headers.

**Autoconf:** retained from v1. SOVERSION bumps from 1 to 2. New `--disable-*` flags follow existing conventions.

**Distribution:** distros package `libhttpserver2` (binary) + `libhttpserver2-dev` / `-devel` (headers). Parallel-installable with `libhttpserver1`.

---
