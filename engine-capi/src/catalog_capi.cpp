// The C interface of the model catalog and of the models on disk.
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "capi_common.hpp"
#include "subtitle_engine/model_catalog.hpp"
#include "subtitle_engine/model_store.hpp"
#include "subtitle_engine_c.h"

using nlohmann::json;
using namespace subtitle_engine;
using namespace subtitle_engine::capi;

struct se_catalog {
  ModelCatalog catalog;
};

namespace {

constexpr const char* kQuick = "quick";
constexpr const char* kFull = "full";

const char* kindName(ModelKind kind) {
  switch (kind) {
    case ModelKind::kTranslator: return "translator";
    case ModelKind::kSpeech: return "speech";
    case ModelKind::kVoiceDetector: return "voice_detector";
  }
  return "";
}

const char* statusName(ModelStatus status) {
  switch (status) {
    case ModelStatus::kInstalled: return "installed";
    case ModelStatus::kMissing: return "missing";
    case ModelStatus::kIncomplete: return "incomplete";
    case ModelStatus::kOutdated: return "outdated";
    case ModelStatus::kCorrupt: return "corrupt";
  }
  return "";
}

// A failure of the C interface: its code goes to error_json.
struct Failure {
  std::string code;
};

void require(bool ok, const char* code = "invalid_argument") {
  if (!ok) throw Failure{code};
}

const ModelEntry& modelOf(se_catalog* c, const char* id) {
  require(id != nullptr);
  const ModelEntry* m = c->catalog.find(id);
  require(m != nullptr, "unknown_model");
  return *m;
}

ModelStore storeOf(const char* modelsDir) {
  require(modelsDir != nullptr && *modelsDir != '\0');
  return ModelStore(modelsDir);
}

// Runs body, turning failures and exceptions into error_json; NULL on failure.
template <typename Body>
char* guarded(se_catalog* catalog, char** errorJson, Body body) {
  try {
    require(catalog != nullptr);
    return duplicate(toJson(body()));
  } catch (const Failure& f) {
    setError(errorJson, f.code);
  } catch (const std::exception&) {
    setError(errorJson, "io_error");
  }
  return nullptr;
}

}  // namespace

extern "C" {

SE_API se_catalog* se_catalog_open(const char* catalogJson, char** errorJson) {
  if (!catalogJson) {
    setError(errorJson, "invalid_argument");
    return nullptr;
  }
  std::string code;
  auto catalog = ModelCatalog::parse(catalogJson, &code);
  if (!catalog) {
    setError(errorJson, code);
    return nullptr;
  }
  return new se_catalog{std::move(*catalog)};
}

SE_API void se_catalog_close(se_catalog* catalog) { delete catalog; }

SE_API char* se_catalog_models(se_catalog* catalog, char** errorJson) {
  return guarded(catalog, errorJson, [&] {
    json out = json::array();
    for (const auto& m : catalog->catalog.models()) {
      json entry = {{"id", m.id},
                    {"kind", kindName(m.kind)},
                    {"version", m.version},
                    {"download_bytes", m.downloadBytes()},
                    {"installed_bytes", m.installedBytes()}};
      if (m.kind == ModelKind::kTranslator) {
        entry["source_language"] = m.sourceLanguage;
        entry["target_language"] = m.targetLanguage;
      }
      out.push_back(entry);
    }
    return out;
  });
}

SE_API char* se_catalog_chain(se_catalog* catalog, const char* source, const char* target, char** errorJson) {
  return guarded(catalog, errorJson, [&] {
    require(source != nullptr && target != nullptr);
    auto chain = catalog->catalog.chain(source, target);
    require(chain.has_value(), "no_chain");
    json out = json::array();
    for (const auto* m : *chain) out.push_back(m->id);
    return out;
  });
}

SE_API char* se_models_check(se_catalog* catalog, const char* modelsDir, const char* idsJson, const char* mode,
                             char** errorJson) {
  return guarded(catalog, errorJson, [&] {
    ModelStore store = storeOf(modelsDir);
    require(mode != nullptr && (std::string(mode) == kQuick || std::string(mode) == kFull));
    const CheckMode checkMode = std::string(mode) == kFull ? CheckMode::kFull : CheckMode::kQuick;
    std::vector<const ModelEntry*> models;
    if (!idsJson) {
      for (const auto& m : catalog->catalog.models()) models.push_back(&m);
    } else {
      const json ids = json::parse(idsJson, nullptr, false);
      require(ids.is_array());
      for (const auto& id : ids) {
        require(id.is_string());
        models.push_back(&modelOf(catalog, id.get<std::string>().c_str()));
      }
    }
    json out = json::array();
    for (const auto* m : models) out.push_back({{"id", m->id}, {"status", statusName(store.check(*m, checkMode))}});
    return out;
  });
}

SE_API char* se_model_downloads(se_catalog* catalog, const char* modelsDir, const char* id, char** errorJson) {
  return guarded(catalog, errorJson, [&] {
    ModelStore store = storeOf(modelsDir);
    json out = json::array();
    for (const auto& t : store.downloads(modelOf(catalog, id))) {
      out.push_back({{"url", t.url}, {"path", t.path}, {"size", t.size}});
    }
    return out;
  });
}

SE_API int se_model_install(se_catalog* catalog, const char* modelsDir, const char* id, char** errorJson) {
  char* done = guarded(catalog, errorJson, [&] {
    ModelStore store = storeOf(modelsDir);
    const std::string code = store.install(modelOf(catalog, id));
    require(code.empty(), code.c_str());
    return json(true);
  });
  se_free(done);
  return done ? 1 : 0;
}

SE_API char* se_catalog_translator_spec(se_catalog* catalog, const char* modelsDir, const char* source,
                                        const char* target, char** errorJson) {
  return guarded(catalog, errorJson, [&] {
    ModelStore store = storeOf(modelsDir);
    require(source != nullptr && target != nullptr);
    auto chain = catalog->catalog.chain(source, target);
    require(chain.has_value(), "no_chain");
    auto spec = store.translatorSpec(*chain);
    require(spec.has_value(), "not_installed");
    return json::parse(*spec);
  });
}

SE_API char* se_model_paths(se_catalog* catalog, const char* modelsDir, const char* id, char** errorJson) {
  return guarded(catalog, errorJson, [&] {
    ModelStore store = storeOf(modelsDir);
    const ModelEntry& m = modelOf(catalog, id);
    require(store.check(m, CheckMode::kQuick) == ModelStatus::kInstalled, "not_installed");
    json out = json::object();
    for (const auto& f : m.files) out[f.role] = store.filePath(m, f);
    return out;
  });
}

}  // extern "C"
