#!/usr/bin/env bash
# Android build (arm64-v8a by default, ANDROID_ABI to change it), stripped, into out/android,
# with the four FFmpeg libraries next to the engine (FFmpeg built first if missing).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NDK="${ANDROID_NDK:-$HOME/Library/Android/sdk/ndk/28.2.13676358}"
ABI="${ANDROID_ABI:-arm64-v8a}"
BUILD="$ROOT/build/android-$ABI"
FFMPEG="$ROOT/build/ffmpeg-android-$ABI"
[[ -f "$FFMPEG/lib/libavformat.so" ]] || ANDROID_NDK="$NDK" ANDROID_ABI="$ABI" "$ROOT/scripts/build_ffmpeg.sh" android >/dev/null
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ABI" -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static -DBUILD_ARCH="${BUILD_ARCH:-armv8-a}" \
  -DSE_BUILD_TESTS=OFF -DSE_BUILD_TOOLS="${SE_BUILD_TOOLS:-OFF}" -DSE_FFMPEG_DIR="$FFMPEG" >/dev/null
CMAKE_POLICY_VERSION_MINIMUM=3.5 ninja -C "$BUILD" "$@"
STRIP="$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip"
mkdir -p "$ROOT/out/android/$ABI"
"$STRIP" -o "$ROOT/out/android/$ABI/libsubtitle_engine.so" "$BUILD/engine-capi/libsubtitle_engine.so"
for lib in avformat avcodec avutil swresample; do
  "$STRIP" -o "$ROOT/out/android/$ABI/lib$lib.so" "$FFMPEG/lib/lib$lib.so"
done
if [[ -f "$BUILD/tools/se_cli" ]]; then "$STRIP" -o "$ROOT/out/android/$ABI/se_cli" "$BUILD/tools/se_cli"; fi
echo "built $ROOT/out/android/$ABI"
