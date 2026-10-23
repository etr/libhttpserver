#!/usr/bin/env bash
# Keep the compiler and its C++20 runtime together through cleaned audit environments.
set -euo pipefail
llvm_prefix=$(brew --prefix llvm)
toolchain_dir="$RUNNER_TEMP/v3-toolchain"
mkdir -p "$toolchain_dir"
{
  printf '#!/usr/bin/env bash\nexec '
  printf '%q ' "$llvm_prefix/bin/clang++" -stdlib=libc++ -D_LIBCPP_DISABLE_AVAILABILITY \
    "-L$llvm_prefix/lib/c++" "-Wl,-rpath,$llvm_prefix/lib/c++"
  printf '"$@"\n'
} > "$toolchain_dir/clang++"
chmod +x "$toolchain_dir/clang++"
cat > "$toolchain_dir/cancellation.cpp" <<'CPP'
#include <stop_token>
#include <thread>
int main() {
    std::stop_source source;
    bool called = false;
    std::stop_callback callback(source.get_token(), [&] { called = true; });
    source.request_stop();
    std::jthread worker([] {});
    return called ? 0 : 1;
}
CPP
"$toolchain_dir/clang++" --version > "$toolchain_dir/compiler.log"
"$toolchain_dir/clang++" -std=c++20 "$toolchain_dir/cancellation.cpp" -o "$toolchain_dir/cancellation"
"$toolchain_dir/cancellation"
printf 'V3_CXX=%s\nV3_CC=%s\n' "$toolchain_dir/clang++" "$llvm_prefix/bin/clang" >> "$GITHUB_ENV"
