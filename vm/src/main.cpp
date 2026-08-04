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
      << "LukeLang\n"
      << "\n"
      << "  " << argv0 << " SHOW  <file.luke>          Play mode  (VM + GC, instant)\n"
      << "  " << argv0 << " BUILD <file.luke> [-o out] Build mode (native, no GC)\n"
      << "\n"
      << "Build is the real language: conversational Luke → C → native binary.\n"
      << "Play is for exploration. See docs/BUILD_MODE.md.\n";
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

std::string replaceExt(std::string path, const std::string &newExt) {
  auto slash = path.find_last_of("/\\");
  auto dot = path.find_last_of('.');
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
    path = path.substr(0, dot);
  }
  return path + newExt;
}

std::string basenameNoExt(std::string path) {
  auto slash = path.find_last_of("/\\");
  if (slash != std::string::npos) path = path.substr(slash + 1);
  auto dot = path.find_last_of('.');
  if (dot != std::string::npos) path = path.substr(0, dot);
  return path;
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

int runBuild(const std::string &path, const std::string &outBin, const std::string &runtimeInclude) {
  std::string source = readFile(path);
  if (source.empty() && !std::ifstream(path)) {
    std::cerr << "Error: could not open " << path << "\n";
    return 1;
  }

  auto built = luke::compileLukeToC(source);
  if (!built.ok) {
    std::cerr << built.error << "\n";
    return 2;
  }

  std::string cPath = replaceExt(outBin.empty() ? basenameNoExt(path) : outBin, ".luke.c");
  if (!outBin.empty()) {
    // place .c next to output name
    cPath = outBin + ".luke.c";
  } else {
    cPath = basenameNoExt(path) + ".luke.c";
  }

  {
    std::ofstream out(cPath);
    if (!out) {
      std::cerr << "Error: could not write " << cPath << "\n";
      return 1;
    }
    out << built.cSource;
  }

  std::string binary = outBin.empty() ? basenameNoExt(path) : outBin;
  std::string cmd = "cc -O2 -std=c11 -I\"" + runtimeInclude + "\" -o \"" + binary + "\" \"" + cPath + "\"";
  std::cerr << "Build: " << cmd << "\n";
  int rc = std::system(cmd.c_str());
  if (rc != 0) {
    std::cerr << "Error: C compile failed (" << rc << ")\n";
    return 4;
  }
  std::cerr << "Build ok → " << binary << " (native, no GC)\n";
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    printUsage(argv[0]);
    return 1;
  }

  // Resolve runtime include path relative to executable when possible.
  std::string runtimeInclude = "runtime";
  {
    // Prefer sibling runtime/ next to binary, else vm/runtime from cwd patterns.
    if (std::ifstream("runtime/luke_rt.h")) runtimeInclude = "runtime";
    else if (std::ifstream("vm/runtime/luke_rt.h")) runtimeInclude = "vm/runtime";
    else if (std::ifstream("../runtime/luke_rt.h")) runtimeInclude = "../runtime";
  }

  std::string cmd = upper(argv[1]);

  // luke SHOW file / luke BUILD file
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
    for (int i = 3; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "-o" && i + 1 < argc) {
        out = argv[++i];
      } else {
        std::cerr << "Unknown BUILD option: " << a << "\n";
        return 1;
      }
    }
    if (path.size() < 5 || path.substr(path.size() - 5) != ".luke") {
      std::cerr << "Error: input must be a .luke file\n";
      return 1;
    }
    return runBuild(path, out, runtimeInclude);
  }

  // Bare file path → Play (compat)
  std::string path = argv[1];
  if (path.size() >= 5 && path.substr(path.size() - 5) == ".luke") {
    return runPlay(path);
  }

  printUsage(argv[0]);
  return 1;
}
