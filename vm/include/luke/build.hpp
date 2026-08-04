#pragma once

#include <string>

namespace luke {

struct BuildResult {
  bool ok = false;
  std::string cSource;   // generated C
  std::string error;
};

// Compile Luke source to standalone C (Build mode: no GC, arena runtime).
// The emitted program #includes "luke_rt.h" and links as a single translation unit
// (header-only runtime) or with the caller's -I path to vm/runtime.
BuildResult compileLukeToC(const std::string &source);

}  // namespace luke
