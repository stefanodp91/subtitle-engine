#include "subtitle_engine/translator.hpp"

#include <fstream>
#include <regex>
#include <sstream>
#include <utility>

#include "common/logging.h"
#include "translator/parser.h"
#include "translator/response.h"
#include "translator/response_options.h"
#include "translator/service.h"
#include "translator/translation_model.h"

namespace subtitle_engine {

namespace bergamot = marian::bergamot;

struct Translator::Impl {
  bergamot::BlockingService service{bergamot::BlockingService::Config{}};
  std::vector<std::shared_ptr<bergamot::TranslationModel>> models;
};

namespace {

bool readable(const std::string& path) { return std::ifstream(path).good(); }

std::string quoted(const std::string& value) {
  std::string out = "\"";
  for (char c : value) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

// The settings Firefox gives its engine (translations-engine.worker.mjs), plus the files.
std::string configYaml(const ModelFiles& files, size_t workspaceMb) {
  std::ostringstream yaml;
  yaml << "models:\n  - " << quoted(files.model) << "\n"
       << "vocabs:\n  - " << quoted(files.vocab) << "\n  - " << quoted(files.vocab) << "\n"
       << "shortlist:\n  - " << quoted(files.lex) << "\n  - false\n"
       << "beam-size: 1\nnormalize: 1.0\nword-penalty: 0\nmax-length-break: 128\n"
       << "mini-batch-words: 1024\nworkspace: " << workspaceMb << "\nmax-length-factor: 2.0\n"
       << "skip-cost: true\ncpu-threads: 0\nquiet: true\nquiet-translation: true\n"
       << "gemm-precision: int8shiftAlphaAll\nalignment: soft\n";
  return yaml.str();
}

bool hasVisibleText(const std::string& line) {
  static const std::regex tags("<[^>]*>");
  const std::string text = std::regex_replace(line, tags, "");
  return text.find_first_not_of(" \t\r\n") != std::string::npos;
}

std::string replaceAll(std::string text, const std::string& from, const std::string& to) {
  for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
    text.replace(at, from.size(), to);
  }
  return text;
}

}  // namespace

Translator::Translator(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Translator::~Translator() = default;

std::unique_ptr<Translator> Translator::open(const TranslatorSpec& spec, std::string* errorCode) {
  if (spec.models.empty() || spec.models.size() > 2 || spec.workspaceMb == 0) {
    *errorCode = "invalid_argument";
    return nullptr;
  }
  for (const auto& files : spec.models) {
    if (!readable(files.model) || !readable(files.vocab) || !readable(files.lex)) {
      *errorCode = "model_load_failed";
      return nullptr;
    }
  }
  marian::setThrowExceptionOnAbort(true);
  auto impl = std::make_unique<Impl>();
  try {
    for (const auto& files : spec.models) {
      auto options = bergamot::parseOptionsFromString(configYaml(files, spec.workspaceMb), /*validate=*/false);
      impl->models.push_back(std::make_shared<bergamot::TranslationModel>(options));
    }
  } catch (const std::exception&) {
    *errorCode = "model_load_failed";
    return nullptr;
  }
  return std::unique_ptr<Translator>(new Translator(std::move(impl)));
}

std::vector<std::string> Translator::translate(const std::vector<std::string>& lines) {
  std::vector<std::string> out(lines);
  std::vector<std::string> sources;
  std::vector<size_t> positions;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (!hasVisibleText(lines[i])) continue;
    sources.push_back(replaceAll(lines[i], "\n", "<br>"));
    positions.push_back(i);
  }
  if (sources.empty()) return out;

  bergamot::ResponseOptions options;
  options.HTML = true;
  std::vector<bergamot::ResponseOptions> perLine(sources.size(), options);
  auto responses = impl_->models.size() == 2
                       ? impl_->service.pivotMultiple(impl_->models[0], impl_->models[1], std::move(sources), perLine)
                       : impl_->service.translateMultiple(impl_->models[0], std::move(sources), perLine);
  for (size_t k = 0; k < responses.size(); ++k) {
    std::string text = responses[k].target.text;
    for (const char* br : {"<br />", "<br/>", "<br>"}) text = replaceAll(text, br, "\n");
    out[positions[k]] = text;
  }
  return out;
}

}  // namespace subtitle_engine
