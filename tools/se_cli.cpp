// Command-line front end over the C interface, for measurements and checks.
//   se_cli translate --spec <json> [--mode batch|lines] < cues.txt > out.txt
//   se_cli tracks <file.mkv>
//   se_cli extract <file.mkv> <track number>
// cues.txt: one cue per line, "<br>" where the cue breaks into rows.
#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "subtitle_engine_c.h"

using nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace {

double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

long maxRssKb() {
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
  return usage.ru_maxrss / 1024;
#else
  return usage.ru_maxrss;
#endif
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
  for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
  return s;
}

int fail(char* error) {
  std::cerr << (error ? error : "error") << "\n";
  se_free(error);
  return 1;
}

int printResult(char* out, char* error) {
  if (!out) return fail(error);
  std::cout << out << "\n";
  se_free(out);
  return 0;
}

int translate(int argc, char** argv) {
  std::string spec, mode = "batch";
  for (int i = 2; i + 1 < argc; i += 2) {
    std::string key = argv[i];
    if (key == "--spec") spec = argv[i + 1];
    else if (key == "--mode") mode = argv[i + 1];
  }
  std::vector<std::string> cues;
  for (std::string line; std::getline(std::cin, line);) {
    if (!line.empty()) cues.push_back(replaceAll(line, "<br>", "\n"));
  }
  char* error = nullptr;
  auto loadStart = Clock::now();
  se_translator* t = se_translator_open(spec.c_str(), &error);
  if (!t) return fail(error);
  double loadMs = msSince(loadStart);

  std::vector<std::string> out;
  std::vector<double> lineMs;
  auto run = [&](const std::vector<std::string>& batch) {
    char* result = se_translate(t, json(batch).dump().c_str(), &error);
    if (!result) return false;
    for (auto& s : json::parse(result).get<std::vector<std::string>>()) out.push_back(replaceAll(s, "\n", "<br>"));
    se_free(result);
    return true;
  };
  auto start = Clock::now();
  bool ok = true;
  if (mode == "lines") {
    for (const auto& cue : cues) {
      auto t0 = Clock::now();
      ok = ok && run({cue});
      lineMs.push_back(msSince(t0));
    }
  } else {
    ok = run(cues);
  }
  double totalMs = msSince(start);
  se_translator_close(t);
  if (!ok) return fail(error);
  for (const auto& s : out) std::cout << s << "\n";

  double avg = 0, p95 = 0, max = 0;
  if (!lineMs.empty()) {
    for (double v : lineMs) avg += v;
    avg /= lineMs.size();
    std::sort(lineMs.begin(), lineMs.end());
    p95 = lineMs[std::min(lineMs.size() - 1, static_cast<size_t>(lineMs.size() * 0.95))];
    max = lineMs.back();
  }
  std::fprintf(stderr,
               "{\"cues\":%zu,\"mode\":\"%s\",\"load_ms\":%.0f,\"total_ms\":%.0f,\"line_ms_avg\":%.1f,"
               "\"line_ms_p95\":%.1f,\"line_ms_max\":%.1f,\"max_rss_kb\":%ld}\n",
               cues.size(), mode.c_str(), loadMs, totalMs, avg, p95, max, maxRssKb());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string command = argc > 1 ? argv[1] : "";
  char* error = nullptr;
  if (command == "translate") return translate(argc, argv);
  if (command == "tracks" && argc == 3) return printResult(se_mkv_text_tracks(argv[2], &error), error);
  if (command == "extract" && argc == 4) {
    return printResult(se_mkv_extract(argv[2], std::strtoull(argv[3], nullptr, 10), &error), error);
  }
  std::cerr << "usage: se_cli translate --spec <json> [--mode batch|lines] | tracks <mkv> | extract <mkv> <n>\n";
  return 2;
}
