#pragma once
#include <string>

#include "subtitle_engine/translator.hpp"

inline std::string modelDir(const std::string& pair) { return std::string(SE_FIXTURES) + "/models/" + pair; }

inline subtitle_engine::ModelFiles modelFiles(const std::string& pair) {
  const std::string dir = modelDir(pair);
  return {dir + "/model." + pair + ".intgemm.alphas.bin", dir + "/vocab." + pair + ".spm",
          dir + "/lex.50.50." + pair + ".s2t.bin"};
}
