/* C interface of the subtitle engine. Strings in and out are UTF-8; every
 * string the engine returns (results and error_json) is freed with se_free. */
#ifndef SUBTITLE_ENGINE_C_H
#define SUBTITLE_ENGINE_C_H

#if defined(SE_BUILDING_LIBRARY)
#define SE_API __attribute__((visibility("default")))
#else
#define SE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Engine version, "major.minor.patch". Static storage: do not free. */
SE_API const char* se_version(void);

/* Frees a string returned by the engine (a result or an error_json). */
SE_API void se_free(char* p);

/* A loaded translator (one direction, or two chained through English). */
typedef struct se_translator se_translator;

/* spec_json: {"models":[{"model":"…","vocab":"…","lex":"…"}],"workspace_mb":128}
 * with one or two models; a model with separate vocabularies adds
 * "target_vocab" (then "vocab" is the source one). Returns NULL on failure and sets *error_json to
 * {"code":"invalid_argument"|"model_load_failed","message":"…"}. */
SE_API se_translator* se_translator_open(const char* spec_json, char** error_json);

/* lines_json: a JSON array of strings (may contain <i>…</i> and \n).
 * Returns a JSON array of the same length, or NULL with *error_json
 * ({"code":"invalid_argument"|"translation_failed"}). */
SE_API char* se_translate(se_translator* translator, const char* lines_json, char** error_json);

SE_API void se_translator_close(se_translator* translator);

/* Text subtitle tracks of a Matroska file (read up to the first cluster):
 * [{"number":3,"codec":"S_TEXT/UTF8","language":"eng","language_bcp47":"en",
 *   "name":"English","default":true,"forced":false,"hearing_impaired":false}].
 * NULL with *error_json code io_error or not_matroska. */
SE_API char* se_mkv_text_tracks(const char* path, char** error_json);

/* Every subtitle track, text or image, in file order (the order players
 * number their subtitle tracks in), with the fields of se_mkv_text_tracks and
 * "text": true|false. NULL with code io_error or not_matroska. */
SE_API char* se_mkv_subtitle_tracks(const char* path, char** error_json);

/* All cues of one text track: {"complete":true,"cues":[{"start_ms":2000,
 * "end_ms":4000,"text":"…"}]}. complete is false for a file cut short.
 * NULL with code io_error, not_matroska, track_not_found or not_text_track. */
SE_API char* se_mkv_extract(const char* path, unsigned long long track_number, char** error_json);

/* Decompresses a .zst file into out_path (written as out_path.part and renamed
 * on success). Returns 1, or 0 with *error_json code io_error, corrupt_data or
 * too_large (more than max_bytes once decompressed); on failure no output file
 * is left behind. */
SE_API int se_zstd_decompress_file(const char* in_path, const char* out_path, unsigned long long max_bytes,
                                   char** error_json);

/* ---- Subtitles from the audio ------------------------------------------- */

/* What a media source's read gives back besides a byte count (> 0). */
#define SE_READ_END 0     /* nothing at or after this offset: the file ends here */
#define SE_READ_WAIT (-1) /* the bytes are not there yet (a download running): asked again later */
#define SE_READ_ERROR (-2)

/* The film, as the caller can read it: the engine never opens a file or a network
 * connection for it. read: up to length bytes at offset into buffer; should not block
 * for long (SE_READ_WAIT instead). size: the whole size in bytes, -1 when unknown.
 * The struct is copied; opaque must stay valid while the se_subtitler is open. */
typedef struct se_media_source {
  void* opaque;
  long long (*read)(void* opaque, unsigned long long offset, unsigned char* buffer, unsigned long long length);
  long long size;
} se_media_source;

enum { SE_STAGE_AUDIO = 0, SE_STAGE_LISTENING = 1, SE_STAGE_TRANSLATING = 2 };
/* The progress of a step: its stage and the fraction of the step done (0 to 1, never decreasing). */
typedef void (*se_progress_fn)(void* opaque, int stage, double fraction);

/* whisper.cpp and the Silero voice detector, loaded once. */
typedef struct se_listener se_listener;

/* spec_json: {"model":"…","vad":"…","threads":2,"vad_threads":1,"alignment_heads":"small",
 * "end_pad_ms":500,"moved_min_ms":1000,"start_before_end_ms":200}; alignment_heads: none, base,
 * small, large_v3_turbo (the model's own). NULL with code invalid_argument or model_load_failed. */
SE_API se_listener* se_listener_open(const char* spec_json, char** error_json);
SE_API void se_listener_close(se_listener* listener);

/* The queue that turns a film's audio into subtitles, one piece per step. The listener (and a
 * translator set later) must stay open while the se_subtitler is.
 * spec_json: {"target_language":"it","track_language":"eng","prompt":"…",
 *   "store":{"path":"…","media_id":"…"},"config":{…}}; every key of config is optional, with
 *   the engine's defaults: piece_ms, cut_min_ms, first_piece_ms, first_cut_ms, first_pause_min_ms,
 *   first_pause_open_ms, pause_min_ms, min_tail_ms, first_probe_every_ms, stall_ms, wait_poll_ms,
 *   read_ahead_ms, retry_languages, temperature_inc, line_chars, max_lines, min_on_screen_ms,
 *   max_on_screen_ms, gap_ms, min_length_ms, slack_ms, cut_margin, break_slack, loop_min_repeats,
 *   loop_keep, loop_phrase_chars, duplicate_window_ms, duplicate_min_words, duplicate_look_back,
 *   repeat_pause_ms, prompt_share, prompt_min_words, prompt_name_min_chars, credit_patterns,
 *   sentence_pause_ms, double_listen, second_piece_ms, double_margin, double_fill_sure, loop_ratio.
 * progress may be NULL. NULL with code invalid_argument, io_error, no_audio, unsupported_codec or
 * needs_end_of_file. */
typedef struct se_subtitler se_subtitler;
SE_API se_subtitler* se_subtitler_open(se_listener* listener, const se_media_source* source, const char* spec_json,
                                       se_progress_fn progress, void* progress_opaque, char** error_json);

/* The translator for the audio's language into the target one (NULL: none). Returns 1, or 0
 * with invalid_argument. The translator stays the caller's. */
SE_API int se_subtitler_set_translator(se_subtitler* subtitler, se_translator* translator, char** error_json);

/* Blocking: hears, cleans, translates and shapes one piece. Returns
 * {"status":"cues"|"end_of_media"|"cancelled"|"translator_needed","from_ms":…,"ready_until_ms":…,
 *  "audio_language":"en","cues":[{"start_ms","end_ms","text"}]}, or NULL with code io_error,
 * no_audio, unsupported_codec, needs_end_of_file, store_failed or decode_error. */
SE_API char* se_subtitler_step(se_subtitler* subtitler, char** error_json);

/* Any thread. Outside the stretch already heard, a running step returns "cancelled". */
SE_API void se_subtitler_seek(se_subtitler* subtitler, long long position_ms);
SE_API void se_subtitler_cancel(se_subtitler* subtitler);
SE_API void se_subtitler_close(se_subtitler* subtitler);

/* The subtitles saved for a film's version: {"audio_language":"en","ranges":[[0,64000]],
 * "cues":[{"start_ms","end_ms","text"}]}, or NULL with code not_found, corrupt_data or other_media. */
SE_API char* se_subtitles_load(const char* path, const char* media_id, const char* target_language, char** error_json);

#ifdef __cplusplus
}
#endif

#endif /* SUBTITLE_ENGINE_C_H */
