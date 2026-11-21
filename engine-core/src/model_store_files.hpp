#pragma once
// Names shared by the model store's check and install.

namespace subtitle_engine {

constexpr const char* kStagingFolder = ".download-se";  // where the caller saves the downloads
constexpr const char* kMarkerFile = "installed.json";   // written last: the model is complete
constexpr const char* kLockSuffix = ".lock";            // <dir>/<id>.lock, held while installing
constexpr const char* kPartSuffix = ".part";
constexpr const char* kZstdSuffix = ".zst";

constexpr const char* kRoleModel = "model";
constexpr const char* kRoleVocab = "vocab";
constexpr const char* kRoleSourceVocab = "srcvocab";
constexpr const char* kRoleTargetVocab = "trgvocab";
constexpr const char* kRoleLex = "lex";

}  // namespace subtitle_engine
