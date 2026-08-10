#include "luke/build.hpp"
#include "luke/compiler.hpp"
#include "luke/heap.hpp"
#include "luke/vm.hpp"

#include <cctype>
#include <cstdio>
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
      << "  " << argv0 << " TEST  <file.luke>            Build + run MAKE SURE checks\n"
      << "  " << argv0 << " PKG init <name>             Create luke_modules/<name> package\n"
      << "  " << argv0 << " PKG install <name>          Install from registry/index.json\n"
      << "  " << argv0 << " PKG publish <name>          Publish luke_modules/<name> to registry\n"
      << "  " << argv0 << " PKG lock                   Write luke.lock from luke_modules/\n"
      << "  " << argv0 << " PUBLISH WEB <file.luke>    Build browser dist (html+wasm+fonts)\n"
      << "  " << argv0 << " IR <file.luke>              Dump Build IR summary\n"
      << "\n"
      << "Build options:\n"
      << "  -o <path>                       output binary / wasm / browser stem\n"
      << "  -target native|wasm|browser     default native\n"
      << "\n"
      << "IMPORT: relative, std/<name>, luke/<package>\n"
      << "Stdlib: files json http server sqlite args env paths process js\n"
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

/* std::system return must be checked — failed mkdir/cp otherwise go unnoticed. */
int runSystem(const std::string &cmd, const char *what) {
  int rc = std::system(cmd.c_str());
  if (rc != 0) {
    std::cerr << "Error: " << what << " failed (exit " << rc << "): " << cmd << "\n";
  }
  return rc;
}

std::string upper(std::string s) {
  for (char &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

std::string trimStr(const std::string &s) {
  std::size_t b = 0;
  while (b < s.size() && std::isspace((unsigned char)s[b])) ++b;
  std::size_t e = s.size();
  while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
  return s.substr(b, e - b);
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
      "runtime/luke_browser_boot.js",
      "../runtime/luke_browser_boot.js",
      "vm/runtime/luke_browser_boot.js",
      "/workspace/vm/runtime/luke_browser_boot.js",
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

std::string jsonQuote(const std::string &s) {
  std::ostringstream o;
  o << '"';
  for (char c : s) {
    if (c == '\\' || c == '"') o << '\\' << c;
    else if (c == '\n') o << "\\n";
    else o << c;
  }
  o << '"';
  return o.str();
}

std::string stripBootForInline(std::string boot) {
  /* Browser <script> cannot include Node shebang / CLI harness. */
  if (boot.rfind("#!", 0) == 0) {
    auto nl = boot.find('\n');
    if (nl != std::string::npos) boot = boot.substr(nl + 1);
  }
  auto pos = boot.find("\nvar isNode");
  if (pos == std::string::npos) pos = boot.find("var isNode");
  if (pos != std::string::npos) boot = boot.substr(0, pos);
  size_t p = 0;
  while ((p = boot.find("</script>", p)) != std::string::npos) {
    boot.replace(p, 9, "<\\/script>");
    p += 10;
  }
  return boot;
}

std::string makeBrowserHtml(const std::string &wasmFile, const luke::BuildResult &page) {
  /* Luke owns title/fonts/CSS/body. Boot is Luke runtime inlined (not app JS). */
  std::ostringstream o;
  std::string title = page.pageTitle.empty() ? "LukeLang" : page.pageTitle;
  o << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
    << "  <meta charset=\"utf-8\" />\n"
    << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n"
    << "  <title>" << escapeHtml(title) << "</title>\n";
  for (auto &f : page.pageFonts) {
    if (f.local) continue;
    o << "  <link rel=\"stylesheet\" href=\"" << escapeHtml(f.hrefOrPath)
      << "\" data-luke=\"bring-font\" />\n";
  }
  o << "  <style>\n";
  for (auto &f : page.pageFonts) {
    if (!f.local) continue;
    std::string rel = f.outRelPath.empty() ? f.hrefOrPath : f.outRelPath;
    o << "@font-face{font-family:\"" << escapeHtml(f.family) << "\";src:url(\"" << escapeHtml(rel)
      << "\") format(\"woff2\");font-display:swap;}\n";
  }
  o << "html,body{margin:0;min-height:100%}\n";
  o << "#luke-out{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0,0,0,0)}\n";
  o << page.pageStyle << "\n";
  o << "  </style>\n</head>\n<body>\n";
  o << "  <div id=\"root\">" << page.pageBody << "</div>\n";
  o << "  <pre id=\"luke-out\"></pre>\n";

  std::string bootSrc = findBrowserLoaderSrc();
  std::string boot = bootSrc.empty() ? "" : stripBootForInline(readFile(bootSrc));
  o << "<script>\n" << boot << "\n";
  o << "var LUKE_WHENS = [";
  for (size_t i = 0; i < page.pageWhens.size(); ++i) {
    if (i) o << ",";
    std::string ev = page.pageWhens[i].event.empty() ? "click" : page.pageWhens[i].event;
    o << "{id:" << jsonQuote(page.pageWhens[i].elementId)
      << ",export:" << jsonQuote(page.pageWhens[i].exportName) << ",event:" << jsonQuote(ev)
      << "}";
  }
  o << "];\n";
  o << "browserBootstrap(\"./" << escapeHtml(wasmFile) << "\", { whens: LUKE_WHENS })"
    << ".catch(function(e){ console.error(e); "
       "var r=document.getElementById('root'); if(r) r.textContent=String(e); });\n";
  o << "</script>\n</body>\n</html>\n";
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
                    cPath + "\"";
  for (auto &lib : built.linkLibs) {
    if (!lib.empty() && lib[0] == '/')
      cmd += " \"" + lib + "\"";
    else
      cmd += " -l" + lib;
  }
  cmd += " 2>/tmp/luke_show_cc.err";
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

int emitBrowserGlue(const std::string &stem, const std::string &wasmBasename,
                    const luke::BuildResult &built, const std::string &sourcePath) {
  std::string outDir = dirnameOf(stem);
  std::string srcDir = dirnameOf(sourcePath);

  /* Copy local Luke font packs beside the HTML. */
  bool needFontsDir = false;
  for (auto &f : built.pageFonts)
    if (f.local) needFontsDir = true;
  if (needFontsDir) {
    std::string mk = "mkdir -p \"" + outDir + "/fonts\"";
    if (runSystem(mk, "mkdir fonts dir") != 0) return 1;
  }
  luke::BuildResult page = built;
  for (auto &f : page.pageFonts) {
    if (!f.local) continue;
    std::string from = f.hrefOrPath;
    if (!from.empty() && from[0] != '/') from = srcDir + "/" + from;
    if (f.outRelPath.empty()) {
      auto slash = f.hrefOrPath.find_last_of("/\\");
      auto base =
          slash == std::string::npos ? f.hrefOrPath : f.hrefOrPath.substr(slash + 1);
      f.outRelPath = "fonts/" + base;
    }
    std::string to = outDir + "/" + f.outRelPath;
    std::string cp = "cp -f \"" + from + "\" \"" + to + "\"";
    if (runSystem(cp, "copy font pack") != 0) return 1;
    std::cerr << "  font pack " << f.family << " → " << to << "\n";
  }

  std::string htmlPath = stem + ".html";
  if (!writeFile(htmlPath, makeBrowserHtml(wasmBasename, page))) {
    std::cerr << "Error: could not write " << htmlPath << "\n";
    return 1;
  }
  /* Boot is inlined into HTML from vm/runtime — no app-authored JS file. */
  std::cerr << "Browser page → " << htmlPath << " (Luke content + runtime boot inlined)\n";
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
    for (auto &lib : built.linkLibs) {
      if (lib.find('/') != std::string::npos || lib.find('.') != std::string::npos)
        cmd += " \"" + lib + "\"";
      else
        cmd += " -l" + lib;
    }
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
    int g = emitBrowserGlue(stem, wasmBase, built, path);
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

  if (cmd == "PUBLISH") {
    if (argc < 3 || upper(argv[2]) != "WEB") {
      std::cerr << "Usage: " << argv[0] << " PUBLISH WEB <file.luke> [-o dist/app]\n";
      std::cerr << "  (For packages: luke PKG publish <name>)\n";
      return 1;
    }
    if (argc < 4) {
      std::cerr << "Usage: " << argv[0] << " PUBLISH WEB <file.luke> [-o dist/app]\n";
      return 1;
    }
    std::string path = argv[3];
    std::string out = "dist/app";
    for (int i = 4; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "-o" && i + 1 < argc) out = argv[++i];
      else {
        std::cerr << "Unknown PUBLISH WEB option: " << a << "\n";
        return 1;
      }
    }
    if (path.size() < 5 || path.substr(path.size() - 5) != ".luke") {
      std::cerr << "Error: input must be a .luke file\n";
      return 1;
    }
    auto slash = out.find_last_of("/\\");
    if (slash != std::string::npos) {
      if (runSystem("mkdir -p \"" + out.substr(0, slash) + "\"", "mkdir PUBLISH WEB out dir") != 0)
        return 1;
    }
    int rc = runBuild(path, out, "browser");
    if (rc != 0) return rc;
    std::cerr << "PUBLISH WEB ok — ship:\n";
    std::cerr << "  " << out << ".html\n";
    std::cerr << "  " << out << ".wasm\n";
    return 0;
  }

  if (cmd == "TEST") {
    if (argc < 3) {
      std::cerr << "Usage: " << argv[0] << " TEST <file.luke>\n";
      return 1;
    }
    std::string path = argv[2];
    if (path.size() < 5 || path.substr(path.size() - 5) != ".luke") {
      std::cerr << "Error: input must be a .luke file\n";
      return 1;
    }
    std::cerr << "TEST via Build (MAKE SURE / TEST … END TEST)\n";
    int built = runViaBuildTemp(path);
    if (built == 0) {
      std::cerr << "All tests passed\n";
      return 0;
    }
    if (built < 0) {
      std::cerr << "TEST: Build failed — fix errors above\n";
      return 2;
    }
    std::cerr << "TEST: MAKE SURE failed (exit " << built << ")\n";
    return 1;
  }

  if (cmd == "IR") {
    if (argc < 3) {
      std::cerr << "Usage: " << argv[0] << " IR <file.luke>\n";
      return 1;
    }
    std::string path = argv[2];
    std::string source = readFile(path);
    if (source.empty() && !std::ifstream(path)) {
      std::cerr << "Error: could not open " << path << "\n";
      return 1;
    }
    auto opt = makeBuildOptions(path, "native");
    auto ir = luke::analyzeLukeBuild(source, opt);
    if (!ir.ok && ir.irSummary.empty()) {
      std::cerr << ir.error << "\n";
      return 2;
    }
    if (!ir.ok) std::cerr << ir.error << "\n";
    std::cout << ir.irSummary;
    if (!ir.expandedSource.empty()) {
      std::string outPath = basenameNoExt(path) + ".luke.ir.txt";
      writeFile(outPath, ir.irSummary + "\n--- expanded ---\n" + ir.expandedSource);
      std::cerr << "Wrote " << outPath << "\n";
    }
    return ir.ok ? 0 : 2;
  }

  if (cmd == "PKG" || cmd == "PACKAGE") {
    if (argc < 3) {
      std::cerr << "Usage: " << argv[0] << " PKG init|install <name>\n";
      return 1;
    }
    std::string sub = upper(argv[2]);
    if (sub == "INIT") {
      if (argc < 4) {
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
      if (runSystem(mk, "mkdir package dir") != 0) return 1;
      std::string pkg = "name=" + name + "\nentry=main.luke\ndescription=Luke package " + name + "\n";
      std::string mainLuke = "// Package: luke/" + name +
                             "\n\nTHIS IS FUNCTION hello GIVES BACK TEXT DO\n"
                             "  GIVE BACK \"Hello from luke/" +
                             name +
                             "\"\n"
                             "END FUNCTION\n";
      if (!writeFile(dir + "/luke.pkg", pkg) || !writeFile(dir + "/main.luke", mainLuke)) {
        std::cerr << "Error: could not write package files\n";
        return 1;
      }
      std::cerr << "Created package luke/" << name << " at " << dir << "\n";
      std::cerr << "  IMPORT luke/" << name << "\n";
      return 0;
    }
    if (sub == "INSTALL") {
      if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " PKG install <name>\n";
        return 1;
      }
      std::string name = argv[3];
      const char *regEnv = std::getenv("LUKE_REGISTRY");
      std::string indexPath = regEnv && *regEnv ? std::string(regEnv) : "";
      if (indexPath.empty()) {
        const char *cands[] = {"registry/index.json", "../registry/index.json",
                               "/workspace/registry/index.json", nullptr};
        for (int i = 0; cands[i]; ++i)
          if (std::ifstream(cands[i])) {
            indexPath = cands[i];
            break;
          }
      }
      if (indexPath.empty()) {
        std::cerr << "Error: no registry index (set LUKE_REGISTRY or add registry/index.json)\n";
        return 1;
      }
      std::string index = readFile(indexPath);
      // Tiny JSON extract: "name": { ... "path":"..." } or "url":"..."
      auto keyPos = index.find("\"" + name + "\"");
      if (keyPos == std::string::npos) {
        std::cerr << "Error: package '" << name << "' not in registry " << indexPath << "\n";
        return 1;
      }
      auto brace = index.find('{', keyPos);
      auto endBrace = index.find('}', brace);
      if (brace == std::string::npos || endBrace == std::string::npos) {
        std::cerr << "Error: malformed registry entry for " << name << "\n";
        return 1;
      }
      std::string entry = index.substr(brace, endBrace - brace);
      auto extract = [&](const char *field) -> std::string {
        auto p = entry.find(std::string("\"") + field + "\"");
        if (p == std::string::npos) return {};
        auto c = entry.find(':', p);
        auto q1 = entry.find('"', c + 1);
        auto q2 = entry.find('"', q1 + 1);
        if (q1 == std::string::npos || q2 == std::string::npos) return {};
        return entry.substr(q1 + 1, q2 - q1 - 1);
      };
      std::string path = extract("path");
      std::string url = extract("url");
      std::string root = "luke_modules";
      if (std::ifstream("../luke_modules") || std::ifstream("../registry/index.json"))
        root = "../luke_modules";
      std::string dest = root + "/" + name;
      std::string mk = "mkdir -p \"" + root + "\"";
      if (runSystem(mk, "mkdir luke_modules") != 0) return 1;
      if (!path.empty()) {
        // Resolve path relative to registry file directory
        std::string regDir = dirnameOf(indexPath);
        std::string srcDir = path;
        if (path[0] != '/') srcDir = regDir + "/../" + path; // index in registry/, path luke_modules/...
        // Prefer path as written from workspace root when running from vm/
        if (std::ifstream(path + "/main.luke") || std::ifstream(path + "/luke.pkg"))
          srcDir = path;
        else if (std::ifstream("../" + path + "/main.luke"))
          srcDir = "../" + path;
        else if (std::ifstream(regDir + "/" + path + "/main.luke"))
          srcDir = regDir + "/" + path;
        std::string cmd = "rm -rf \"" + dest + "\" && cp -R \"" + srcDir + "\" \"" + dest + "\"";
        std::cerr << "PKG install: " << cmd << "\n";
        if (runSystem(cmd, "PKG install copy") != 0) return 1;
      } else if (!url.empty()) {
        std::string cmd = "rm -rf \"" + dest + "\" && mkdir -p \"" + dest +
                          "\" && curl -fsSL \"" + url + "\" -o /tmp/luke_pkg.tgz && "
                          "tar -xzf /tmp/luke_pkg.tgz -C \"" + dest + "\" --strip-components=1";
        std::cerr << "PKG install from url...\n";
        if (runSystem(cmd, "PKG install download") != 0) return 1;
      } else {
        std::cerr << "Error: registry entry needs path or url\n";
        return 1;
      }
      std::cerr << "Installed luke/" << name << " → " << dest << "\n";
      std::cerr << "  IMPORT luke/" << name << "\n";
      return 0;
    }
    if (sub == "PUBLISH") {
      if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " PKG publish <name>\n";
        return 1;
      }
      std::string name = argv[3];
      std::string root = "luke_modules";
      if (std::ifstream("../luke_modules/greeter/luke.pkg") || std::ifstream("../luke_modules"))
        root = "../luke_modules";
      std::string src = root + "/" + name;
      if (!std::ifstream(src + "/luke.pkg") && !std::ifstream(src + "/main.luke")) {
        std::cerr << "Error: no local package at " << src << " — luke PKG init " << name << "\n";
        return 1;
      }
      const char *regEnv = std::getenv("LUKE_REGISTRY");
      std::string indexPath = regEnv && *regEnv ? std::string(regEnv) : "";
      if (indexPath.empty()) {
        const char *cands[] = {"registry/index.json", "../registry/index.json",
                               "/workspace/registry/index.json", nullptr};
        for (int i = 0; cands[i]; ++i)
          if (std::ifstream(cands[i])) {
            indexPath = cands[i];
            break;
          }
      }
      if (indexPath.empty()) {
        std::string regDir = (root == "../luke_modules") ? "../registry" : "registry";
        if (runSystem("mkdir -p \"" + regDir + "/packages\"", "mkdir registry packages") != 0)
          return 1;
        indexPath = regDir + "/index.json";
        writeFile(indexPath,
                  "{\n  \"name\": \"luke-registry\",\n  \"version\": \"0.1.0\",\n  "
                  "\"packages\": {}\n}\n");
      }
      std::string regDir = dirnameOf(indexPath);
      std::string dest = regDir + "/packages/" + name;
      if (runSystem("mkdir -p \"" + regDir + "/packages\"", "mkdir registry packages") != 0)
        return 1;
      std::string copy = "rm -rf \"" + dest + "\" && cp -R \"" + src + "\" \"" + dest + "\"";
      if (runSystem(copy, "PKG publish copy") != 0) return 1;
      std::string version = "0.1.0";
      std::string pkgMeta = readFile(src + "/luke.pkg");
      auto vp = pkgMeta.find("version=");
      if (vp != std::string::npos) {
        auto end = pkgMeta.find('\n', vp);
        version = trimStr(pkgMeta.substr(
            vp + 8, end == std::string::npos ? std::string::npos : end - (vp + 8)));
      }
      std::string index = readFile(indexPath);
      std::string relPath = "registry/packages/" + name;
      auto packages = index.find("\"packages\"");
      if (packages == std::string::npos) {
        std::cerr << "Error: registry index missing packages object\n";
        return 1;
      }
      auto brace = index.find('{', packages);
      auto existing = index.find("\"" + name + "\"");
      if (existing != std::string::npos && existing > brace) {
        auto eb = index.find('{', existing);
        auto ee = index.find('}', eb);
        if (eb != std::string::npos && ee != std::string::npos) {
          index.replace(eb, ee - eb + 1,
                        "{\n      \"version\": \"" + version +
                            "\",\n      \"description\": \"Published luke/" + name +
                            "\",\n      \"path\": \"" + relPath + "\"\n    }");
        }
      } else {
        size_t i = brace + 1;
        int depth = 1;
        size_t packagesEnd = std::string::npos;
        for (; i < index.size(); ++i) {
          if (index[i] == '{')
            depth++;
          else if (index[i] == '}') {
            depth--;
            if (depth == 0) {
              packagesEnd = i;
              break;
            }
          }
        }
        if (packagesEnd == std::string::npos) {
          std::cerr << "Error: malformed packages object\n";
          return 1;
        }
        bool empty = true;
        for (size_t j = brace + 1; j < packagesEnd; ++j) {
          if (!std::isspace((unsigned char)index[j])) {
            empty = false;
            break;
          }
        }
        std::string entry = "    \"" + name + "\": {\n      \"version\": \"" + version +
                            "\",\n      \"description\": \"Published luke/" + name +
                            "\",\n      \"path\": \"" + relPath + "\"\n    }";
        std::string insert = empty ? ("\n" + entry + "\n  ") : (",\n" + entry + "\n  ");
        index.insert(packagesEnd, insert);
      }
      if (!writeFile(indexPath, index)) {
        std::cerr << "Error: could not update " << indexPath << "\n";
        return 1;
      }
      std::cerr << "Published luke/" << name << " @" << version << " → " << dest << "\n";
      std::cerr << "  registry: " << indexPath << "\n";
      return 0;
    }
    if (sub == "LOCK") {
      std::string root = "luke_modules";
      if (std::ifstream("../luke_modules") || std::ifstream("../registry/index.json"))
        root = "../luke_modules";
      std::string outPath = (root == "../luke_modules") ? "../luke.lock" : "luke.lock";
      std::string listCmd = "ls -1 \"" + root + "\" 2>/dev/null";
      FILE *pipe = popen(listCmd.c_str(), "r");
      if (!pipe) {
        std::cerr << "Error: could not list " << root << "\n";
        return 1;
      }
      std::ostringstream lock;
      lock << "# luke.lock — installed package pins\n";
      lock << "lock_version=1\n";
      char buf[512];
      while (fgets(buf, sizeof(buf), pipe)) {
        std::string name = buf;
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r' || name.back() == ' '))
          name.pop_back();
        if (name.empty() || name[0] == '.') continue;
        std::string dir = root + "/" + name;
        if (!std::ifstream(dir + "/luke.pkg") && !std::ifstream(dir + "/main.luke")) continue;
        std::string version = "0.0.0";
        auto meta = readFile(dir + "/.luke-installed");
        if (meta.empty()) meta = readFile(dir + "/luke.pkg");
        auto vp = meta.find("version=");
        if (vp != std::string::npos) {
          auto end = meta.find('\n', vp);
          version = trimStr(meta.substr(
              vp + 8, end == std::string::npos ? std::string::npos : end - (vp + 8)));
        }
        lock << "package " << name << " version " << version << "\n";
      }
      pclose(pipe);
      if (!writeFile(outPath, lock.str())) {
        std::cerr << "Error: could not write " << outPath << "\n";
        return 1;
      }
      std::cerr << "Wrote " << outPath << "\n";
      return 0;
    }
    std::cerr << "Usage: " << argv[0] << " PKG init|install|publish|lock …\n";
    return 1;
  }

  std::string path = argv[1];
  if (path.size() >= 5 && path.substr(path.size() - 5) == ".luke") {
    return runPlay(path, false);
  }

  printUsage(argv[0]);
  return 1;
}
