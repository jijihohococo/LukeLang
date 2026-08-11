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

struct BrowserFont {
  std::string family;
  std::string hrefOrPath;   // remote URL or local path as written in source
  std::string resolvedPath; // resolved filesystem path when local
  std::string outRelPath;   // e.g. fonts/syne-700.woff2 beside HTML
  bool local = false;
};

struct BrowserWhen {
  std::string elementId;
  std::string exportName; // luke_when_0
  std::string event;      // click | change | submit | route
  std::vector<std::string> body;
  std::vector<size_t> lines;
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

  /* Browser page owned by Luke (content baked into HTML; boot is runtime). */
  bool hasPage = false;
  std::string pageTitle;
  std::string pageStyle;
  std::string pageBody; // FILL "root" markup
  std::vector<BrowserFont> pageFonts;
  std::vector<BrowserWhen> pageWhens;
};

// Compile Luke source to standalone C (Build mode: no GC, arena runtime).
BuildResult compileLukeToC(const std::string &source, const BuildOptions &options = {});

std::string expandLukeImports(const std::string &source, const BuildOptions &options,
                              BuildResult *meta = nullptr);

std::string softenBuildSurfaceForPlay(const std::string &expanded);

BuildResult analyzeLukeBuild(const std::string &source, const BuildOptions &options = {});

/* Minimal stdio JSON-RPC language server (diagnostics beachhead). */
int runLspStdio(const BuildOptions &baseOpts = {});

}  // namespace luke
