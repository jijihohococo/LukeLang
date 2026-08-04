#pragma once

#include "luke/bytecode.hpp"
#include "luke/heap.hpp"
#include "luke/value.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace luke {

enum class InterpretResult {
  Ok,
  CompileError,
  RuntimeError,
};

class VM {
 public:
  explicit VM(Heap &heap);

  InterpretResult interpret(Chunk &chunk);
  InterpretResult run();

  Heap &heap() { return heap_; }
  const std::unordered_map<std::string, Value> &globals() const { return globals_; }

  // For GC root tracing and tests.
  void push(Value value);
  Value pop();
  Value peek(int distance = 0) const;

 private:
  void runtimeError(const std::string &message);
  bool binaryArith(Op op);
  uint8_t readByte();
  uint16_t readShort();
  Value readConstant();

  Heap &heap_;
  Chunk *chunk_ = nullptr;
  std::size_t ip_ = 0;
  std::vector<Value> stack_;
  std::unordered_map<std::string, Value> globals_;
  bool hadError_ = false;
};

}  // namespace luke
