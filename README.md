# subtitle-engine

An engine for subtitles: it translates subtitle lines on the device, offline, with the Firefox Translations models, and reads the text tracks inside Matroska (MKV) files. Built for apps: a C interface with JSON data, and a Dart package that drives it from a dedicated isolate.

Version `0.2.0`. License: [Mozilla Public License 2.0](LICENSE), the same as Mozilla's translation engine it builds on.

## Layout

| Folder | What |
|---|---|
| `engine-core/` | C++17. `Translator` (one language pair, or two chained through English) on top of Mozilla's engine; `listTextTracks` / `extractTextTrack` for MKV files |
| `engine-capi/` | The C interface, `subtitle_engine_c.h`, with JSON data; the `libsubtitle_engine` library |
| `flutter_subtitle_engine/` | Dart package calling the C interface through `dart:ffi` from a dedicated isolate |
| `tools/se_cli.cpp` | Command-line tool for measurements and checks |
| `tests/` | GoogleTest tests, run on macOS without network |
| `scripts/` | Dependencies, test translators, test videos, builds |
| `patches/` | Fixes to the dependencies, applied by `fetch_deps.sh` |
| `fixtures/` | Three test MKVs (SRT, ASS, WebVTT) and `short_en.srt`; `fixtures/models/` (downloaded, ignored) |

## Setup

```bash
scripts/fetch_deps.sh        # dependencies pinned in scripts/deps.lock, with the patches
scripts/fetch_test_models.sh # Firefox en→it, it→en, es→en, zh-Hant→en into fixtures/models/
scripts/make_fixtures.sh     # rebuilds the three test MKVs (needs ffmpeg)
```

Every dependency has a version or a SHA-256 hash in `scripts/deps.lock`; every test translator has the hashes of its compressed and decompressed file in `scripts/test_models.json`. A different hash stops the script.

## Build and test

```bash
scripts/build_mac.sh se_tests && build/mac/tests/se_tests          # with AddressSanitizer
cmake -S . -B build/mac-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DSE_BUILD_TESTS=OFF && ninja -C build/mac-release subtitle_engine se_cli
(cd flutter_subtitle_engine && dart test)                            # uses build/mac-release
scripts/build_android.sh subtitle_engine                             # out/android/arm64-v8a/libsubtitle_engine.so
SE_BUILD_TOOLS=ON scripts/build_android.sh subtitle_engine se_cli   # se_cli for Android too
```

Android: `arm64-v8a` by default (other ABIs with `ANDROID_ABI`), `android-29`, NDK `28.2.13676358`, `c++_static`. The library exports only the `se_*` functions.

## The C interface

Every string the engine returns, errors included, is freed with `se_free`. Errors are `{"code":"…","message":"…"}` and never contain subtitle text or file paths.

| Function | Input | Output |
|---|---|---|
| `se_version()` | | `"0.2.0"` |
| `se_translator_open(spec_json, &err)` | `{"models":[{"model","vocab","lex"}],"workspace_mb":128}`, one or two models; a model with separate vocabularies adds `"target_vocab"` (`"vocab"` is then the source one) | a translator, or `NULL` with `invalid_argument` / `model_load_failed` |
| `se_translate(t, lines_json, &err)` | an array of strings, with `<i>…</i>` and `\n` | an array of the same length; lines without visible text come back unchanged |
| `se_translator_close(t)` | | |
| `se_mkv_text_tracks(path, &err)` | an MKV file | `[{"number","codec","language","language_bcp47","name","default","forced","hearing_impaired"}]`, or `io_error` / `not_matroska` |
| `se_zstd_decompress_file(in, out, max_bytes, &err)` | a `.zst` file (the Firefox models ship this way) | `1`, or `0` with `io_error` / `corrupt_data` / `too_large`; the output is written to `out.part` and renamed only on success |
| `se_mkv_extract(path, n, &err)` | an MKV file and a track number | `{"complete":true,"cues":[{"start_ms","end_ms","text"}]}`, or `io_error` / `not_matroska` / `track_not_found` / `not_text_track` |

Text tracks: `S_TEXT/UTF8`, `S_TEXT/ASS`, `S_TEXT/SSA`, `S_TEXT/WEBVTT` (also the older `D_WEBVTT/…` mapping ffmpeg writes, reported as `S_TEXT/WEBVTT`). For ASS/SSA the text is the `Text` field, with `\N` turned into a line break and the `{\…}` override tags left to the caller. Blocks compressed with zlib or with header stripping, unknown-size clusters and truncated files (`complete: false`) are handled.

## Dependencies and licenses

| Dependency | Version | License |
|---|---|---|
| Mozilla's translation engine (`inference/` of `mozilla/translations`, with Marian, sentencepiece, intgemm, ruy, cpuinfo, simd_utils, onnxjs, ssplit-cpp) | commit `8eada038f` | MPL 2.0 (the engine), open licenses for the submodules |
| Firefox Translations models (tests only) | `base-memory` 3.0 | MPL 2.0 |
| PCRE2 | 10.39 | BSD |
| nlohmann/json | 3.11.3 | MIT |
| zstd (decompression only; compression in the tests) | 1.5.6 | BSD |
| GoogleTest (tests only) | 1.15.2 | BSD |

## Patches

| Patch | Why |
|---|---|
| `0001-marian-no-werror` | Current Clang reports new warnings in Marian's code: without `-Werror` the build goes on |
| `0002-sentencepiece-constexpr` | A `constexpr` that newer compilers reject, in a sentencepiece file used only for training |
| `0003-zlib-apple-guard` | Marian's bundled zlib mistakes macOS for classic Mac OS and redefines `fdopen` |
| `0004-ssplit-pcre2-local-tarball` | PCRE2 from the tarball already downloaded and checked, never during the build; `CMAKE_POLICY_VERSION_MINIMUM` for its build |

On top of that, the top-level `CMakeLists.txt` sets `ARM FMA SSE` and `-flax-vector-conversions` for the whole project on ARM (Marian sets them only for its own folder) and `CMAKE_POLICY_VERSION_MINIMUM=3.5` for old CMake files; the tests build GoogleTest with `GTEST_HAS_POSIX_RE=0`, because the engine's folders carry a `regex.h` of their own.

## Measurements (Android arm64, Snapdragon 855)

English → Italian, 1450 lines: 10.9 s; one line at a time: 43 ms on average, 138 ms at most; peak memory about 220 MB. The `workspace_mb` value (128, 64, 32, 16) changes neither time, memory nor translations: it stays at 128, Firefox's value. On the device two runs give the same output; on macOS they do not (a few lines change a word from one run to the next).
