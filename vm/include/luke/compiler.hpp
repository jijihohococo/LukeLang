#pragma once

#include "luke/bytecode.hpp"
#include "luke/heap.hpp"

#include <string>

namespace luke {

struct CompileResult {
  bool ok = false;
  Chunk chunk;
  std::string error;
};

// Compiles LukeLang into bytecode for the native VM.
// Supported: SPEAK, variables, arithmetic, lists, IF/WHILE, functions,
// blueprints (HAS, WHEN BORN, METHOD, NEW, ASK TO, SELF, CALL PARENT).
CompileResult compileLuke(const std::string &source, Heap &heap);

}  // namespace luke
