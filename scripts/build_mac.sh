#!/usr/bin/env bash
# Mac build with tests (AddressSanitizer on), into build/mac.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$ROOT" -B "$ROOT/build/mac" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DSE_BUILD_TESTS=ON -DSE_BUILD_TOOLS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address" >/dev/null
ninja -C "$ROOT/build/mac" "$@"
