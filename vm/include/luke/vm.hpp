#pragma once

#include "luke/bytecode.hpp"
#include "luke/function.hpp"
#include "luke/heap.hpp"
#include "luke/object.hpp"
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

struct CallFrame {
  ObjClosure *closure = nullptr;  // null for top-level script
  ObjFunction *function = nullptr;
  Chunk *chunk = nullptr;
  std::size_t ip = 0;
  std::size_t slots = 0;
};

class VM {
 public:
  static constexpr int kFramesMax = 64;

  explicit VM(Heap &heap);

  InterpretResult interpret(Chunk &chunk);
  InterpretResult run();

  Heap &heap() { return heap_; }
  const std::unordered_map<std::string, Value> &globals() const { return globals_; }

  void push(Value value);
  Value pop();
  Value peek(int distance = 0) const;

 private:
  void runtimeError(const std::string &message);
  bool binaryArith(Op op);
  bool callValue(Value callee, int argCount);
  bool call(ObjFunction *function, ObjClosure *closure, int argCount);
  bool invokeFromClass(ObjClass *klass, const std::string &name, int argCount, bool allowPrivate);
  bool canAccessPrivate(ObjClass *owner) const;
  bool bindMethod(ObjClass *klass, const std::string &name, bool allowPrivate);
  void applyFieldDefaults(ObjInstance *instance, ObjClass *klass);
  ObjUpvalue *captureUpvalue(Value *local);
  void closeUpvalues(Value *last);
  uint8_t readByte();
  uint16_t readShort();
  Value readConstant();
  CallFrame &frame();

  Heap &heap_;
  Chunk *scriptChunk_ = nullptr;
  std::vector<CallFrame> frames_;
  std::vector<Value> stack_;
  std::unordered_map<std::string, Value> globals_;
  ObjUpvalue *openUpvalues_ = nullptr;
  bool hadError_ = false;
};

}  // namespace luke
