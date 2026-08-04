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
// Supported:
//   SPEAK / variables / arithmetic / lists / IF / WHILE
//   THIS IS FUNCTION name WITH a, b DO ... GIVE BACK expr ... END FUNCTION
//   ASK name WITH args  (calls; method form deferred to blueprints)
CompileResult compileLuke(const std::string &source, Heap &heap);

}  // namespace luke
