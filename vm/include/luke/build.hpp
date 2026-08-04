#pragma once

#include <string>
#include <vector>

namespace luke {

struct BuildOptions {
  std::string sourcePath;   // used to resolve relative IMPORT paths
  std::string stdlibPath;   // directory containing std/*.luke (optional)
  bool forWasm = false;     // tweak entry / notes in generated C if needed
};

struct BuildResult {
  bool ok = false;
  std::string cSource;
  std::string error;
  std::vector<std::string> importedFiles;
};

// Compile Luke source to standalone C (Build mode: no GC, arena runtime).
// Supports IMPORT "rel/path.luke" and IMPORT std/<name>.
BuildResult compileLukeToC(const std::string &source, const BuildOptions &options = {});

}  // namespace luke
