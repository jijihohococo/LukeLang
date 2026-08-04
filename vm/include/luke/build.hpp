#pragma once

#include <string>
#include <vector>

namespace luke {

struct BuildOptions {
  std::string sourcePath;   // used to resolve relative IMPORT paths
  std::string stdlibPath;   // directory containing std/*.luke (optional)
  std::vector<std::string> packagePaths; // luke_modules roots for IMPORT luke/<name>
  bool forWasm = false;     // WASI / browser wasm artifact
  bool forBrowser = false;  // also emit browser glue (html/js) via CLI
};

struct BuildResult {
  bool ok = false;
  std::string cSource;
  std::string error;
  std::vector<std::string> importedFiles;
  bool unsupportedForBuild = false; // Play-only feature; SHOW may fall back to VM
};

// Compile Luke source to standalone C (Build mode: no GC, arena runtime).
// Supports IMPORT "rel/path.luke", IMPORT std/<name>, IMPORT luke/<pkg>.
BuildResult compileLukeToC(const std::string &source, const BuildOptions &options = {});

}  // namespace luke
