#!/usr/bin/env bash
# Builds the audio-only FFmpeg the engine reads films with (LGPL 2.1, shared
# libraries, no GPL or non-free parts, no network) into build/ffmpeg-<mac|android>/
# (include/ and lib/). Sources from scripts/fetch_deps.sh.
#   build_ffmpeg.sh mac
#   build_ffmpeg.sh android   (ANDROID_NDK, ANDROID_ABI=arm64-v8a)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/third_party/ffmpeg"
PLATFORM="${1:?usage: build_ffmpeg.sh mac|android}"
[[ -x "$SRC/configure" ]] || { echo "Run scripts/fetch_deps.sh first" >&2; exit 1; }
OPTS=(--disable-everything --disable-programs --disable-doc --disable-network --disable-avdevice
  --disable-swscale --disable-avfilter --enable-shared --disable-static --enable-protocol=file
  --enable-demuxer=matroska,mov,avi,mpegts
  --enable-decoder=aac,ac3,eac3,dca,truehd,opus,mp3,flac,vorbis,pcm_s16le,pcm_s24le
  --enable-parser=aac,ac3,dca,opus,mpegaudio,flac,vorbis)
case "$PLATFORM" in
  mac)
    OUT="$ROOT/build/ffmpeg-mac" ;;
  android)
    NDK="${ANDROID_NDK:-$HOME/Library/Android/sdk/ndk/28.2.13676358}"
    ABI="${ANDROID_ABI:-arm64-v8a}"
    [[ "$ABI" == "arm64-v8a" ]] || { echo "only arm64-v8a is supported" >&2; exit 1; }
    T="$NDK/toolchains/llvm/prebuilt/darwin-x86_64"
    OUT="$ROOT/build/ffmpeg-android-$ABI"
    OPTS+=(--enable-cross-compile --target-os=android --arch=aarch64 --cpu=armv8-a
      --cc="$T/bin/aarch64-linux-android29-clang" --cxx="$T/bin/aarch64-linux-android29-clang++"
      --nm="$T/bin/llvm-nm" --ar="$T/bin/llvm-ar" --ranlib="$T/bin/llvm-ranlib" --strip="$T/bin/llvm-strip"
      --sysroot="$T/sysroot") ;;
  *) echo "usage: build_ffmpeg.sh mac|android" >&2; exit 1 ;;
esac
OBJ="$OUT-obj"
mkdir -p "$OBJ"
( cd "$OBJ" && "$SRC/configure" --prefix="$OUT" "${OPTS[@]}" > configure.log 2>&1 \
  && make -j"$(sysctl -n hw.ncpu)" > make.log 2>&1 && make install > install.log 2>&1 )
if grep -qE 'CONFIG_(GPL|NONFREE) 1' "$OBJ/config.h"; then echo "FFmpeg was configured with GPL or non-free parts" >&2; exit 1; fi
echo "built $OUT"
