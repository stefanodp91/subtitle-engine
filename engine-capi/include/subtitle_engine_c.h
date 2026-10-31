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
 * with one or two models. Returns NULL on failure and sets *error_json to
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

/* All cues of one text track: {"complete":true,"cues":[{"start_ms":2000,
 * "end_ms":4000,"text":"…"}]}. complete is false for a file cut short.
 * NULL with code io_error, not_matroska, track_not_found or not_text_track. */
SE_API char* se_mkv_extract(const char* path, unsigned long long track_number, char** error_json);

#ifdef __cplusplus
}
#endif

#endif /* SUBTITLE_ENGINE_C_H */
