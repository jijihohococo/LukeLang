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

// Compiles a subset of LukeLang into bytecode for the native VM.
// Supported (v0 native):
//   SPEAK expr
//   MY NAME IS name [SET TO expr]
//   SET name TO expr
//   numbers, quoted strings, ADD/SUBTRACT/AND (concat), comparisons
//   IF condition DO ... END IF
//   WHILE condition DO ... END WHILE
//   THIS IS FUNCTION name WITH a, b DO ... GIVE BACK expr ... END FUNCTION
//   ASK name WITH args  /  name(args) style via ASK
CompileResult compileLuke(const std::string &source, Heap &heap);

}  // namespace luke
