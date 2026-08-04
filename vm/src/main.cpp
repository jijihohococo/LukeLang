#include "luke/build.hpp"
#include "luke/compiler.hpp"
#include "luke/heap.hpp"
#include "luke/vm.hpp"

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void printUsage(const char *argv0) {
  std::cerr
      << "LukeLang — Build is real; Play is convenience\n"
      << "\n"
      << "  " << argv0 << " SHOW  <file.luke> [--vm]     Run (Build-first; VM fallback)\n"
      << "  " << argv0 << " BUILD <file.luke> [options]  Build (native / wasm / browser)\n"
      << "  " << argv0 << " PKG init <name>             Create luke_modules/<name> package\n"
      << "\n"
      << "Build options:\n"
      << "  -o <path>                       output binary / wasm / browser stem\n"
      << "  -target native|wasm|browser     default native\n"
      << "\n"
      << "IMPORT: relative, std/<name>, luke/<package>\n"
      << "Stdlib: files json http args env paths process js\n"
      << "See docs/BUILD_MODE.md\n";
}

std::string readFile(const std::string &path) {
  std::ifstream in(path);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool writeFile(const std::string &path, const std::string &data) {
  std::ofstream out(path);
  if (!out) return false;
  out << data;
  return true;
}

std::string upper(std::string s) {
  for (char &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

std::string basenameNoExt(std::string path) {
  auto slash = path.find_last_of("/\\");
  if (slash != std::string::npos) path = path.substr(slash + 1);
  auto dot = path.find_last_of('.');
  if (dot != std::string::npos) path = path.substr(0, dot);
  return path;
}

std::string dirnameOf(std::string path) {
  auto slash = path.find_last_of("/\\");
  if (slash == std::string::npos) return ".";
  return path.substr(0, slash);
}

std::string findRuntimeInclude() {
  if (std::ifstream("runtime/luke_rt.h")) return "runtime";
  if (std::ifstream("vm/runtime/luke_rt.h")) return "vm/runtime";
  if (std::ifstream("../runtime/luke_rt.h")) return "../runtime";
  return "runtime";
}

std::string findStdlib() {
  if (std::ifstream("stdlib/files.luke")) return "stdlib";
  if (std::ifstream("vm/stdlib/files.luke")) return "vm/stdlib";
  if (std::ifstream("../stdlib/files.luke")) return "../stdlib";
  return "stdlib";
}

std::string findWasiClang() {
  const char *env = std::getenv("LUKE_WASI_SDK");
  if (env && *env) {
    std::string p = std::string(env) + "/bin/clang";
    if (std::ifstream(p)) return p;
  }
  const char *candidates[] = {
      "/workspace/.tools/wasi-sdk/bin/clang",
      ".tools/wasi-sdk/bin/clang",
      "../.tools/wasi-sdk/bin/clang",
      nullptr,
  };
  for (int i = 0; candidates[i]; ++i) {
    if (std::ifstream(candidates[i])) return candidates[i];
  }
  return {};
}

std::string findBrowserLoaderSrc() {
  const char *candidates[] = {
      "../scripts/luke_browser_loader.cjs",
      "scripts/luke_browser_loader.cjs",
      "/workspace/scripts/luke_browser_loader.cjs",
      nullptr,
  };
  for (int i = 0; candidates[i]; ++i) {
    if (std::ifstream(candidates[i])) return candidates[i];
  }
  return {};
}

luke::BuildOptions makeBuildOptions(const std::string &path, const std::string &target) {
  luke::BuildOptions opt;
  opt.sourcePath = path;
  opt.stdlibPath = findStdlib();
  opt.forWasm = (target == "wasm" || target == "browser");
  opt.forBrowser = (target == "browser");
  opt.packagePaths.push_back(dirnameOf(path) + "/luke_modules");
  opt.packagePaths.push_back("luke_modules");
  opt.packagePaths.push_back("../luke_modules");
  return opt;
}

std::string escapeHtml(const std::string &s) {
  std::string o;
  for (char c : s) {
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o.push_back(c);
  }
  return o;
}

std::string makeBrowserHtml(const std::string &wasmFile, const std::string &title) {
  std::ostringstream o;
  o << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
    << "  <meta charset=\"utf-8\" />\n"
    << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n"
    << "  <title>" << escapeHtml(title) << " — Luke</title>\n"
    << "  <style>\n"
    << "    :root { color-scheme: light; --ink:#1a1a1a; --paper:#f7f4ef; --accent:#0b6e4f; }\n"
    << "    * { box-sizing: border-box; }\n"
    << "    body { margin:0; min-height:100vh; font-family: \"Iowan Old Style\", \"Palatino Linotype\", "
       "Palatino, Georgia, serif;\n"
    << "      background: radial-gradient(1200px 600px at 10% -10%, #dfece6 0%, transparent 55%),\n"
    << "                 radial-gradient(900px 500px at 100% 0%, #f0e6d8 0%, transparent 50%),\n"
    << "                 var(--paper); color: var(--ink); }\n"
    << "    main { max-width: 42rem; margin: 0 auto; padding: 12vh 1.5rem 4rem; }\n"
    << "    .brand { font-size: clamp(2.5rem, 8vw, 4rem); font-weight: 700; letter-spacing: -0.03em;\n"
    << "      color: var(--accent); margin: 0 0 0.35rem; animation: rise 0.7s ease-out both; }\n"
    << "    h1 { font-size: 1.15rem; font-weight: 500; margin: 0 0 1.5rem; opacity: 0.75;\n"
    << "      animation: rise 0.7s ease-out 0.08s both; }\n"
    << "    #luke-status, #luke-panel { margin: 0.75rem 0; animation: rise 0.7s ease-out 0.12s both; }\n"
    << "    #luke-out { font-family: \"Source Code Pro\", ui-monospace, monospace; font-size: 0.95rem;\n"
    << "      white-space: pre-wrap; line-height: 1.45; padding: 1.25rem 0; border-top: 1px solid "
       "rgba(0,0,0,0.12);\n"
    << "      animation: rise 0.7s ease-out 0.16s both; }\n"
    << "    @keyframes rise { from { opacity:0; transform: translateY(0.6rem); } to { opacity:1; "
       "transform:none; } }\n"
    << "  </style>\n</head>\n<body>\n<main>\n"
    << "  <p class=\"brand\">Luke</p>\n"
    << "  <h1>" << escapeHtml(title) << "</h1>\n"
    << "  <p id=\"luke-status\"></p>\n"
    << "  <div id=\"luke-panel\"></div>\n"
    << "  <pre id=\"luke-out\"></pre>\n"
    << "</main>\n"
    << "<script src=\"./luke_browser_loader.js\"></script>\n"
    << "<script>\n"
    << "  browserBootstrap(\"./" << escapeHtml(wasmFile) << "\")"
    << ".catch(function(e){ var el=document.getElementById('luke-out'); "
       "el.textContent += String(e); console.error(e); });\n"
    << "</script>\n</body>\n</html>\n";
  return o.str();
}

int runViaVm(const std::string &path) {
  std::string source = readFile(path);
  if (source.empty() && !std::ifstream(path)) {
    std::cerr << "Error: could not open " << path << "\n";
    return 1;
  }
  luke::Heap heap;
  auto compiled = luke::compileLuke(source, heap);
  if (!compiled.ok) {
    std::cerr << compiled.error << "\n";
    return 2;
  }
  luke::VM vm(heap);
  if (vm.interpret(compiled.chunk) == luke::InterpretResult::RuntimeError) return 3;
  return 0;
}

int runViaBuildTemp(const std::string &path) {
  std::string source = readFile(path);
  if (source.empty() && !std::ifstream(path)) {
    std::cerr << "Error: could not open " << path << "\n";
    return 1;
  }
  auto opt = makeBuildOptions(path, "native");
  auto built = luke::compileLukeToC(source, opt);
  if (!built.ok) {
    std::cerr << built.error << "\n";
    return built.unsupportedForBuild ? -2 : -1;
  }

  std::string stem = "/tmp/luke_show_" + basenameNoExt(path) + "_" + std::to_string(std::rand());
  std::string cPath = stem + ".c";
  std::string binPath = stem + ".bin";
  if (!writeFile(cPath, built.cSource)) {
    std::cerr << "Error: could not write temp Build output\n";
    return 1;
  }
  std::string runtimeInclude = findRuntimeInclude();
  std::string cmd = "cc -O2 -std=gnu11 -I\"" + runtimeInclude + "\" -o \"" + binPath + "\" \"" +
                    cPath + "\" 2>/tmp/luke_show_cc.err";
  int rc = std::system(cmd.c_str());
  if (rc != 0) {
    std::cerr << "Build (SHOW) compile failed — falling back to Play VM\n";
    std::remove(cPath.c_str());
    return -2;
  }
  std::cerr << "SHOW via Build (native, no GC)\n";
  for (auto &imp : built.importedFiles) std::cerr << "  imported " << imp << "\n";
  rc = std::system(binPath.c_str());
  std::remove(cPath.c_str());
  std::remove(binPath.c_str());
  return rc == 0 ? 0 : 3;
}

int runPlay(const std::string &path, bool forceVm) {
  if (forceVm || std::getenv("LUKE_SHOW_VM")) {
    std::cerr << "SHOW via Play VM (GC)\n";
    return runViaVm(path);
  }
  int built = runViaBuildTemp(path);
  if (built >= 0) return built;
  // Build couldn't run it (Play-only feature, or incomplete Build support) → VM.
  std::cerr << "(SHOW: falling back to Play VM compatibility layer)\n";
  std::cerr << "SHOW via Play VM (compatibility layer)\n";
  return runViaVm(path);
}

int emitBrowserGlue(const std::string &stem, const std::string &wasmBasename) {
  std::string loaderSrc = findBrowserLoaderSrc();
  if (loaderSrc.empty()) {
    std::cerr << "Error: could not find scripts/luke_browser_loader.cjs\n";
    return 1;
  }
  std::string loader = readFile(loaderSrc);
  std::string outDir = dirnameOf(stem);
  std::string loaderOut = outDir + "/luke_browser_loader.js";
  // Browser <script src> wants .js; content is plain JS (CJS guards are inert in browsers).
  if (!writeFile(loaderOut, loader)) {
    std::cerr << "Error: could not write " << loaderOut << "\n";
    return 1;
  }
  std::string htmlPath = stem + ".html";
  // wasm file may be stem.wasm — basename for relative URL
  if (!writeFile(htmlPath, makeBrowserHtml(wasmBasename, basenameNoExt(stem)))) {
    std::cerr << "Error: could not write " << htmlPath << "\n";
    return 1;
  }
  std::cerr << "Browser glue → " << htmlPath << " + " << loaderOut << "\n";
  return 0;
}

int runBuild(const std::string &path, const std::string &outBin, const std::string &target) {
  std::string source = readFile(path);
  if (source.empty() && !std::ifstream(path)) {
    std::cerr << "Error: could not open " << path << "\n";
    return 1;
  }

  auto opt = makeBuildOptions(path, target);
  auto built = luke::compileLukeToC(source, opt);
  if (!built.ok) {
    std::cerr << built.error << "\n";
    return 2;
  }
  for (auto &imp : built.importedFiles) {
    std::cerr << "  imported " << imp << "\n";
  }

  std::string binary = outBin.empty() ? basenameNoExt(path) : outBin;
  bool wasmOut = (target == "wasm" || target == "browser");
  if (wasmOut) {
    if (binary.size() >= 5 && binary.substr(binary.size() - 5) == ".wasm") {
      // ok
    } else if (binary.find('.') == std::string::npos) {
      binary += ".wasm";
    } else if (target == "browser" && binary.size() >= 5 &&
               binary.substr(binary.size() - 5) != ".wasm") {
      // stem given without .wasm — append
      binary += ".wasm";
    }
  }
  std::string cPath = binary + ".luke.c";
  if (wasmOut && binary.size() >= 5 && binary.substr(binary.size() - 5) == ".wasm")
    cPath = binary.substr(0, binary.size() - 5) + ".luke.c";

  if (!writeFile(cPath, built.cSource)) {
    std::cerr << "Error: could not write " << cPath << "\n";
    return 1;
  }

  std::string runtimeInclude = findRuntimeInclude();
  std::string cmd;
  if (wasmOut) {
    std::string clang = findWasiClang();
    if (clang.empty()) {
      std::cerr << "Error: WASM/browser target needs WASI SDK.\n"
                << "  Set LUKE_WASI_SDK to your wasi-sdk root, or install under .tools/wasi-sdk\n";
      return 5;
    }
    cmd = "\"" + clang + "\" -O2 -o \"" + binary + "\" -I\"" + runtimeInclude + "\" \"" + cPath + "\"";
  } else {
    cmd = "cc -O2 -std=gnu11 -I\"" + runtimeInclude + "\" -o \"" + binary + "\" \"" + cPath + "\"";
  }

  std::cerr << "Build: " << cmd << "\n";
  int rc = std::system(cmd.c_str());
  if (rc != 0) {
    std::cerr << "Error: compile failed (" << rc << ")\n";
    return 4;
  }

  if (target == "browser") {
    std::string stem = binary;
    if (stem.size() >= 5 && stem.substr(stem.size() - 5) == ".wasm")
      stem = stem.substr(0, stem.size() - 5);
    std::string wasmBase = basenameNoExt(binary) + ".wasm";
    // If binary is path/foo.wasm, html sits next to it as path/foo.html
    int g = emitBrowserGlue(stem, wasmBase);
    if (g != 0) return g;
    std::cerr << "Build ok → " << binary << " (browser wasm, no GC)\n";
    std::cerr << "  Open " << stem << ".html in a browser (or: node ../scripts/luke_browser_loader.cjs "
              << binary << ")\n";
  } else if (target == "wasm") {
    std::cerr << "Build ok → " << binary << " (wasm/wasi, no GC)\n";
  } else {
    std::cerr << "Build ok → " << binary << " (native, no GC)\n";
  }
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  std::srand((unsigned)std::time(nullptr));
  if (argc < 2) {
    printUsage(argv[0]);
    return 1;
  }

  std::string cmd = upper(argv[1]);

  if (cmd == "SHOW" || cmd == "PLAY") {
    if (argc < 3) {
      printUsage(argv[0]);
      return 1;
    }
    std::string path = argv[2];
    bool forceVm = false;
    for (int i = 3; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "--vm" || a == "-vm") forceVm = true;
      else {
        std::cerr << "Unknown SHOW option: " << a << "\n";
        return 1;
      }
    }
    if (path.size() < 5 || path.substr(path.size() - 5) != ".luke") {
      std::cerr << "Error: input must be a .luke file\n";
      return 1;
    }
    return runPlay(path, forceVm);
  }

  if (cmd == "BUILD") {
    if (argc < 3) {
      printUsage(argv[0]);
      return 1;
    }
    std::string path = argv[2];
    std::string out;
    std::string target = "native";
    for (int i = 3; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "-o" && i + 1 < argc) {
        out = argv[++i];
      } else if (a == "-target" && i + 1 < argc) {
        target = argv[++i];
        for (char &c : target) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (target != "native" && target != "wasm" && target != "browser") {
          std::cerr << "Error: -target must be native, wasm, or browser\n";
          return 1;
        }
      } else {
        std::cerr << "Unknown BUILD option: " << a << "\n";
        return 1;
      }
    }
    if (path.size() < 5 || path.substr(path.size() - 5) != ".luke") {
      std::cerr << "Error: input must be a .luke file\n";
      return 1;
    }
    return runBuild(path, out, target);
  }

  if (cmd == "PKG" || cmd == "PACKAGE") {
    if (argc < 4 || upper(argv[2]) != "INIT") {
      std::cerr << "Usage: " << argv[0] << " PKG init <name>\n";
      return 1;
    }
    std::string name = argv[3];
    for (char c : name) {
      if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) {
        std::cerr << "Error: package name must be alphanumeric / _ / -\n";
        return 1;
      }
    }
    std::string root = "luke_modules";
    if (std::ifstream("../luke_modules/greeter/luke.pkg") || std::ifstream("../luke_modules"))
      root = "../luke_modules";
    std::string dir = root + "/" + name;
    if (std::ifstream(dir + "/main.luke") || std::ifstream(dir + "/luke.pkg")) {
      std::cerr << "Error: package already exists at " << dir << "\n";
      return 1;
    }
    std::string mk = "mkdir -p \"" + dir + "\"";
    if (std::system(mk.c_str()) != 0) {
      std::cerr << "Error: could not create " << dir << "\n";
      return 1;
    }
    std::string pkg = "name=" + name + "\nentry=main.luke\ndescription=Luke package " + name + "\n";
    std::string mainLuke =
        "// Package: luke/" + name + "\n\n"
        "THIS IS FUNCTION hello GIVES BACK TEXT DO\n"
        "  GIVE BACK \"Hello from luke/" + name + "\"\n"
        "END FUNCTION\n";
    if (!writeFile(dir + "/luke.pkg", pkg) || !writeFile(dir + "/main.luke", mainLuke)) {
      std::cerr << "Error: could not write package files\n";
      return 1;
    }
    std::cerr << "Created package luke/" << name << " at " << dir << "\n";
    std::cerr << "  IMPORT luke/" << name << "\n";
    return 0;
  }

  std::string path = argv[1];
  if (path.size() >= 5 && path.substr(path.size() - 5) == ".luke") {
    return runPlay(path, false);
  }

  printUsage(argv[0]);
  return 1;
}
