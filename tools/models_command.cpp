// se_cli models --catalog <models.json> --dir <models dir> <command>
//   list | chain <src> <tgt> | check [--full] [ids…] | downloads <id> | install <id> | spec <src> <tgt>
// check also prints {"elapsed_ms":…,"bytes":…} on a second line: bytes is what an
// installed model holds, all of it read by a full check.
#include "models_command.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "subtitle_engine_c.h"

using nlohmann::json;

namespace {

int usage() {
  std::cerr << "usage: se_cli models --catalog <models.json> --dir <models dir>\n"
               "         list | chain <src> <tgt> | check [--full] [ids...] | downloads <id> | install <id>\n"
               "         | spec <src> <tgt>\n";
  return 2;
}

int fail(char* error) {
  std::cerr << (error ? error : "error") << "\n";
  se_free(error);
  return 1;
}

int print(char* out, char* error) {
  if (!out) return fail(error);
  std::cout << out << "\n";
  se_free(out);
  return 0;
}

int check(se_catalog* catalog, const std::string& dir, const std::vector<std::string>& args) {
  std::string mode = "quick";
  json ids = json::array();
  for (const auto& a : args) {
    if (a == "--full") mode = "full";
    else ids.push_back(a);
  }
  char* error = nullptr;
  const auto start = std::chrono::steady_clock::now();
  char* out = se_models_check(catalog, dir.c_str(), ids.empty() ? nullptr : ids.dump().c_str(), mode.c_str(), &error);
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  if (!out) return fail(error);
  const json statuses = json::parse(out);
  std::cout << out << "\n";
  se_free(out);

  std::map<std::string, unsigned long long> sizes;
  char* models = se_catalog_models(catalog, &error);
  if (!models) return fail(error);
  for (const auto& m : json::parse(models)) sizes[m["id"]] = m["installed_bytes"];
  se_free(models);
  unsigned long long bytes = 0;
  for (const auto& s : statuses) {
    if (s["status"] == "installed") bytes += sizes[s["id"]];
  }
  std::printf("{\"elapsed_ms\":%.1f,\"bytes\":%llu}\n", ms, bytes);
  return 0;
}

int run(se_catalog* catalog, const std::string& dir, const std::string& command, const std::vector<std::string>& args) {
  char* error = nullptr;
  if (command == "list" && args.empty()) return print(se_catalog_models(catalog, &error), error);
  if (command == "chain" && args.size() == 2) {
    return print(se_catalog_chain(catalog, args[0].c_str(), args[1].c_str(), &error), error);
  }
  if (command == "check") return check(catalog, dir, args);
  if (command == "downloads" && args.size() == 1) {
    return print(se_model_downloads(catalog, dir.c_str(), args[0].c_str(), &error), error);
  }
  if (command == "install" && args.size() == 1) {
    if (!se_model_install(catalog, dir.c_str(), args[0].c_str(), &error)) return fail(error);
    std::cout << "installed " << args[0] << "\n";
    return 0;
  }
  if (command == "spec" && args.size() == 2) {
    return print(se_catalog_translator_spec(catalog, dir.c_str(), args[0].c_str(), args[1].c_str(), &error), error);
  }
  return usage();
}

}  // namespace

int modelsCommand(int argc, char** argv) {
  std::string catalogPath, dir, command;
  std::vector<std::string> args;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--catalog" && i + 1 < argc) catalogPath = argv[++i];
    else if (a == "--dir" && i + 1 < argc) dir = argv[++i];
    else if (command.empty()) command = a;
    else args.push_back(a);
  }
  if (catalogPath.empty() || command.empty() || (command != "list" && command != "chain" && dir.empty())) {
    return usage();
  }
  std::ifstream in(catalogPath, std::ios::binary);
  if (!in) {
    std::cerr << "cannot read the catalog\n";
    return 1;
  }
  const std::string text((std::istreambuf_iterator<char>(in)), {});
  char* error = nullptr;
  se_catalog* catalog = se_catalog_open(text.c_str(), &error);
  if (!catalog) return fail(error);
  const int code = run(catalog, dir, command, args);
  se_catalog_close(catalog);
  return code;
}
