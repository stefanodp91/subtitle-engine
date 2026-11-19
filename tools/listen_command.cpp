// se_cli listen --listener <json> --spec <json> --input <video> [--translator <json>] [--from-ms N]
//               [--seconds S] [--bytes-per-sec B] [--srt <out.srt>] [--stats <out.jsonl>]
// Reads the video through an se_media_source over the file; with --bytes-per-sec the
// readable part grows like a download (SE_READ_WAIT past it). The cues go only to --srt;
// --stats gets one line per step and a final one. Nothing of the speech reaches stdout or stderr.
#include "listen_command.hpp"

#include <sys/resource.h>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

#include "subtitle_engine_c.h"

using nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace {

struct GrowingFile {  // a file whose readable part grows at bytesPerSec from the start
  std::FILE* file = nullptr;
  long long size = 0;
  double bytesPerSec = 0;
  Clock::time_point start = Clock::now();
};

long long readGrowing(void* opaque, unsigned long long offset, unsigned char* buffer, unsigned long long length) {
  auto* f = static_cast<GrowingFile*>(opaque);
  long long limit = f->size;
  if (f->bytesPerSec > 0) {
    const double s = std::chrono::duration<double>(Clock::now() - f->start).count();
    limit = std::min<long long>(f->size, static_cast<long long>(s * f->bytesPerSec));
  }
  if (static_cast<long long>(offset) >= f->size) return SE_READ_END;
  if (static_cast<long long>(offset) >= limit) return SE_READ_WAIT;
  const unsigned long long want = std::min<unsigned long long>(length, static_cast<unsigned long long>(limit) - offset);
  if (std::fseek(f->file, static_cast<long>(offset), SEEK_SET) != 0) return SE_READ_ERROR;
  const size_t got = std::fread(buffer, 1, static_cast<size_t>(want), f->file);
  return got > 0 ? static_cast<long long>(got) : SE_READ_ERROR;
}

long maxRssKb() {
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
  return usage.ru_maxrss / 1024;
#else
  return usage.ru_maxrss;
#endif
}

std::string srtTime(long long ms) {
  char out[32];
  std::snprintf(out, sizeof(out), "%02lld:%02lld:%02lld,%03lld", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000);
  return out;
}

int fail(char* error) {
  std::cerr << (error ? error : "{\"code\":\"error\"}") << "\n";  // codes and messages only, never subtitle text
  se_free(error);
  return 1;
}

}  // namespace

int listenCommand(int argc, char** argv) {
  std::string listenerSpec, spec, input, translatorSpec, srtPath, statsPath;
  long long fromMs = 0;
  double seconds = 0, bytesPerSec = 0;
  for (int i = 2; i + 1 < argc; i += 2) {
    const std::string key = argv[i], value = argv[i + 1];
    if (key == "--listener") listenerSpec = value;
    else if (key == "--spec") spec = value;
    else if (key == "--input") input = value;
    else if (key == "--translator") translatorSpec = value;
    else if (key == "--from-ms") fromMs = std::stoll(value);
    else if (key == "--seconds") seconds = std::stod(value);
    else if (key == "--bytes-per-sec") bytesPerSec = std::stod(value);
    else if (key == "--srt") srtPath = value;
    else if (key == "--stats") statsPath = value;
  }
  const auto start = Clock::now();
  auto atMs = [&] { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count(); };
  GrowingFile file;
  file.file = std::fopen(input.c_str(), "rb");
  if (!file.file) return fail(nullptr);
  std::fseek(file.file, 0, SEEK_END);
  file.size = std::ftell(file.file);
  file.bytesPerSec = bytesPerSec;
  char* error = nullptr;
  se_listener* listener = se_listener_open(listenerSpec.c_str(), &error);
  if (!listener) return fail(error);
  se_translator* translator = nullptr;
  if (!translatorSpec.empty() && !(translator = se_translator_open(translatorSpec.c_str(), &error))) return fail(error);
  se_media_source source{&file, readGrowing, file.size};
  se_subtitler* sub = se_subtitler_open(listener, &source, spec.c_str(), nullptr, nullptr, &error);
  if (!sub) return fail(error);
  se_subtitler_set_translator(sub, translator, &error);
  if (fromMs > 0) se_subtitler_seek(sub, fromMs);
  std::FILE* srt = srtPath.empty() ? nullptr : std::fopen(srtPath.c_str(), "w");
  std::FILE* stats = statsPath.empty() ? nullptr : std::fopen(statsPath.c_str(), "w");
  int cueNumber = 0, steps = 0, code = 0;
  long long readyUntil = fromMs;
  for (;;) {
    const auto t0 = Clock::now();
    char* out = se_subtitler_step(sub, &error);
    if (!out) {
      code = fail(error);
      break;
    }
    const json step = json::parse(out);
    se_free(out);
    const std::string status = step.at("status");
    if (status == "translator_needed") {
      code = fail(nullptr);
      break;
    }
    ++steps;
    readyUntil = step.at("ready_until_ms");
    for (const auto& c : step.at("cues"))
      if (srt) std::fprintf(srt, "%d\n%s --> %s\n%s\n\n", ++cueNumber, srtTime(c.at("start_ms")).c_str(),
                            srtTime(c.at("end_ms")).c_str(), c.at("text").get<std::string>().c_str());
    if (stats)
      std::fprintf(stats, "%s\n",
                   json{{"from_ms", step.at("from_ms")}, {"ready_until_ms", readyUntil},
                        {"wall_ms", std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count()},
                        {"cues", step.at("cues").size()}, {"status", status}, {"max_rss_kb", maxRssKb()}, {"at_ms", atMs()}}
                       .dump()
                       .c_str());
    if (srt) std::fflush(srt);
    if (stats) std::fflush(stats);
    if (status != "cues" || (seconds > 0 && readyUntil - fromMs >= static_cast<long long>(seconds * 1000))) break;
  }
  if (stats)
    std::fprintf(stats, "%s\n",
                 json{{"total", true}, {"steps", steps}, {"cues", cueNumber}, {"ready_until_ms", readyUntil},
                      {"wall_ms", atMs()}, {"max_rss_kb", maxRssKb()}}
                     .dump()
                     .c_str());
  if (srt) std::fclose(srt);
  if (stats) std::fclose(stats);
  se_subtitler_close(sub);
  se_translator_close(translator);
  se_listener_close(listener);
  std::fclose(file.file);
  return code;
}
