# Changelog

All notable changes to subtitle-engine are documented in this file. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Released versions are the tags on `main`
(`v0.4.0`, `v0.4.2`, `v0.4.5`, `v0.5.0`); the versions in between were numbered on `develop` and went out with the
next release.

## Unreleased

### Added

- This changelog.

## 0.5.0 - 2026-11-27

### Added

- Saved subtitles say the speech model that made them (`store.model` in the subtitler's spec, format 2 of the
  file): a file made by another model is heard again. `se_subtitles_load_store` reads a store's cues only when they
  are its model's (`other_model` otherwise); `se_subtitles_load` still reads them whatever model made them.
- Each step of the subtitler says how long its save took (`save_ms`, the file on the disk included) and how it spent
  its time (`timing`: the wait for the piece, the speech detector's probes, the recogniser, the translator, the
  piece's length and its speech, and whether a cue packed too dense cut it short).

### Changed

- A save is durable: the `.part` file is synced to the disk before the rename, and its folder after.

### Fixed

- Saved cues always lie inside their stretches, and a jump no longer leaves cues written twice.

## 0.4.5 - 2026-11-23

### Fixed

- A place in a film is found through its video's index, not by reading from the start.

## 0.4.4 - 2026-11-23

### Fixed

- A jump into missing audio starts where the audio is, and no piece is heard empty.

## 0.4.3 - 2026-11-23

### Fixed

- The audio language waits for speech, and is detected on the whole piece.

## 0.4.2 - 2026-11-22

### Added

- The end of the media says why, in numbers, for diagnostics (`end_detail`).

## 0.4.1 - 2026-11-22

### Fixed

- A seek into a saved stretch goes on from its end.

## 0.4.0 - 2026-11-21

### Added

- The model catalog (`catalog/models.json`: the Firefox translators, whisper small `q6_K`, Silero) and its
  generator; the catalog read with SHA-256 fingerprints; check (quick or full), download targets and install for
  its models; the C interface (`se_catalog_*`, `se_models_check`, `se_model_downloads`, `se_model_install`,
  `se_catalog_translator_spec`, `se_model_paths`) and `se_cli models`.

### Fixed

- A sentence the translation model writes twice comes back once.

## 0.3.0 - 2026-11-19

### Added

- Subtitles from the audio: the audio of a video read through a caller's read function (FFmpeg, audio only),
  speech heard with whisper.cpp and cut into pieces at pauses, cues cleaned, joined and shaped, the
  heard-to-translated queue one piece at a time with jumps and stalls, generated subtitles kept per video version
  and resumed; the C interface (`se_listener_*`, `se_subtitler_*`, `se_subtitles_load`) and `se_cli listen`.

### Changed

- The double listen is off by default.

### Fixed

- The resampler is set up again when a track's channels, format or rate change; every heard time stays within its
  piece's audio; a cue packed denser than speech at a piece's end is heard again; the first cut after a start, a
  jump or a stall no longer depends on when the audio came.

## 0.2.0 - 2026-10-31

### Added

- zstd decompression (`se_zstd_decompress_file`) and an exported symbol list: the library exports only the `se_*`
  functions.
- Models with separate source and target vocabularies.
- Every subtitle track of a Matroska file, text or image, in file order (`se_mkv_subtitle_tracks`).

### Security

- The Matroska reader and the C interface hardened against hostile files.

## 0.1.0 - 2026-10-31

### Added

- Offline translation with Mozilla's translation engine and the Firefox Translations models
  (`se_translator_open`, `se_translate`), the text tracks of a Matroska file (`se_mkv_text_tracks`,
  `se_mkv_extract`), the C interface in JSON, `se_cli` and the Dart package `flutter_subtitle_engine`.
