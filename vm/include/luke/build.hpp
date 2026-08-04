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
  bool expandStd = true;    // Play may set false (no native helpers)
  bool expandCImports = true;
};

struct BuildResult {
  bool ok = false;
  std::string cSource;
  std::string error;
  std::vector<std::string> importedFiles;
  std::vector<std::string> linkLibs;   // e.g. "m", "sqlite3", or path to .a
  std::vector<std::string> foreignFns; // names of FOREIGN FUNCTION symbols
  std::string expandedSource;          // Build IR text after IMPORT expansion
  std::string irSummary;               // human/machine IR dump
  bool unsupportedForBuild = false;    // Play-only feature; SHOW may fall back to VM
};

// Compile Luke source to standalone C (Build mode: no GC, arena runtime).
// Supports IMPORT relative / std/ / luke/ / c:<lib>, and FOREIGN FUNCTION.
BuildResult compileLukeToC(const std::string &source, const BuildOptions &options = {});

// Shared frontend: expand IMPORT lines (used by Build and Play).
// On failure, sets result.error and returns {}.
std::string expandLukeImports(const std::string &source, const BuildOptions &options,
                              BuildResult *meta = nullptr);

// Strip Build-only annotations so Play's bytecode compiler can ingest Build IR text.
std::string softenBuildSurfaceForPlay(const std::string &expanded);

// Analyze / dump Build IR without requiring a full native link.
BuildResult analyzeLukeBuild(const std::string &source, const BuildOptions &options = {});

}  // namespace luke
