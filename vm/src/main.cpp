#include "luke/compiler.hpp"
#include "luke/heap.hpp"
#include "luke/vm.hpp"

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void printUsage(const char *argv0) {
  std::cerr
      << "LukeLang native runtime\n"
      << "Usage:\n"
      << "  " << argv0 << " <file.luke>\n"
      << "  " << argv0 << " SHOW <file.luke>\n"
      << "\n"
      << "Runs LukeLang on its own VM (no Node/JavaScript host).\n";
}

std::string readFile(const std::string &path) {
  std::ifstream in(path);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    printUsage(argv[0]);
    return 1;
  }

  std::string path;
  if (argc >= 3) {
    std::string cmd = argv[1];
    for (char &c : cmd) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (cmd == "SHOW") {
      path = argv[2];
    } else {
      printUsage(argv[0]);
      return 1;
    }
  } else {
    path = argv[1];
  }

  if (path.size() < 5 || path.substr(path.size() - 5) != ".luke") {
    std::cerr << "Error: input must be a .luke file\n";
    return 1;
  }

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
  auto result = vm.interpret(compiled.chunk);
  if (result == luke::InterpretResult::RuntimeError) return 3;
  return 0;
}
