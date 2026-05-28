## 14) Appendices

### A. Glossary

- **PIMPL:** Pointer-to-Implementation idiom. Public class holds `std::unique_ptr<impl>`; impl is defined in a private header. Hides backend types and implementation details.
- **SBO:** Small-Buffer Optimization. Inline aligned buffer holding a small object via placement new, avoiding heap allocation.
- **Radix tree:** Compressed trie data structure used here for path-segment matching with wildcards and prefix support.
- **method_set:** Wrapper around a `uint32_t` bitmask indexed by `http_method` enum values.
- **SOVERSION:** Linker-level shared-object version; bumping signals binary incompatibility.

### B. References

- PRD: `specs/product_specs.md`
- libmicrohttpd: <https://www.gnu.org/software/libmicrohttpd/>
- Existing v1 source tree: `src/`
- C++20 standard library reference: <https://en.cppreference.com/w/cpp/20>
- Crow (route-table radix-tree reference): <https://github.com/CrowCpp/Crow>
- userver (FastPimpl / arena PIMPL reference): <https://userver.tech>
- Boost.Beast (header-hygiene reference): <https://github.com/boostorg/beast>
