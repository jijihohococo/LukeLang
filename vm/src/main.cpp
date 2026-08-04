#include "luke/build.hpp"
#include "luke/compiler.hpp"
#include "luke/heap.hpp"
#include "luke/vm.hpp"

#include <cctype>
#include <cstdlib>
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
      << "  " << argv0 << " SHOW  <file.luke>              Play (VM + GC)\n"
      << "  " << argv0 << " BUILD <file.luke> [options]    Build (native / wasm, no GC)\n"
      << "\n"
      << "Build options:\n"
      << "  -o <path>              output binary / wasm path\n"
      << "  -target native|wasm    default native (host) or wasm (WASI)\n"
      << "\n"
      << "IMPORT std/files, std/json, or relative .luke modules in Build sources.\n"
      << "See docs/BUILD_MODE.md\n";
}

std::string readFile(const std::string &path) {
  std::ifstream in(path);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
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

int runPlay(const std::string &path) {
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

int runBuild(const std::string &path, const std::string &outBin, const std::string &target) {
  std::string source = readFile(path);
  if (source.empty() && !std::ifstream(path)) {
    std::cerr << "Error: could not open " << path << "\n";
    return 1;
  }

  luke::BuildOptions opt;
  opt.sourcePath = path;
  opt.stdlibPath = findStdlib();
  opt.forWasm = (target == "wasm");

  auto built = luke::compileLukeToC(source, opt);
  if (!built.ok) {
    std::cerr << built.error << "\n";
    return 2;
  }
  for (auto &imp : built.importedFiles) {
    std::cerr << "  imported " << imp << "\n";
  }

  std::string binary = outBin.empty() ? basenameNoExt(path) : outBin;
  if (opt.forWasm && binary.size() >= 5 && binary.substr(binary.size() - 5) != ".wasm") {
    // leave as-is if user gave full name; if no extension, add .wasm
    if (binary.find('.') == std::string::npos) binary += ".wasm";
  }
  std::string cPath = binary + ".luke.c";

  {
    std::ofstream out(cPath);
    if (!out) {
      std::cerr << "Error: could not write " << cPath << "\n";
      return 1;
    }
    out << built.cSource;
  }

  std::string runtimeInclude = findRuntimeInclude();
  std::string cmd;
  if (opt.forWasm) {
    std::string clang = findWasiClang();
    if (clang.empty()) {
      std::cerr << "Error: WASM target needs WASI SDK.\n"
                << "  Set LUKE_WASI_SDK to your wasi-sdk root, or install under .tools/wasi-sdk\n"
                << "  Example: https://github.com/WebAssembly/wasi-sdk/releases\n";
      return 5;
    }
    cmd = "\"" + clang + "\" -O2 -o \"" + binary + "\" -I\"" + runtimeInclude + "\" \"" + cPath + "\"";
  } else {
    cmd = "cc -O2 -std=c11 -I\"" + runtimeInclude + "\" -o \"" + binary + "\" \"" + cPath + "\"";
  }

  std::cerr << "Build: " << cmd << "\n";
  int rc = std::system(cmd.c_str());
  if (rc != 0) {
    std::cerr << "Error: compile failed (" << rc << ")\n";
    return 4;
  }
  std::cerr << "Build ok → " << binary << (opt.forWasm ? " (wasm/wasi, no GC)\n" : " (native, no GC)\n");
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
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
    if (path.size() < 5 || path.substr(path.size() - 5) != ".luke") {
      std::cerr << "Error: input must be a .luke file\n";
      return 1;
    }
    return runPlay(path);
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
        if (target != "native" && target != "wasm") {
          std::cerr << "Error: -target must be native or wasm\n";
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

  std::string path = argv[1];
  if (path.size() >= 5 && path.substr(path.size() - 5) == ".luke") {
    return runPlay(path);
  }

  printUsage(argv[0]);
  return 1;
}
