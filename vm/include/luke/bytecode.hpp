#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace luke {

enum class Op : uint8_t {
  Constant = 1,  // operand: const index
  Nil,
  True,
  False,
  Pop,
  GetGlobal,   // operand: name const index
  SetGlobal,   // operand: name const index
  GetLocal,    // operand: slot
  SetLocal,    // operand: slot
  GetUpvalue,  // operand: upvalue slot
  SetUpvalue,  // operand: upvalue slot
  Add,
  Subtract,
  Multiply,
  Divide,
  Negate,
  Not,
  Equal,
  Greater,
  Less,
  Print,       // SPEAK
  Jump,        // operand: offset (uint16)
  JumpIfFalse, // operand: offset (uint16)
  Loop,        // operand: back offset (uint16)
  Call,        // operand: arg count
  Closure,     // operand: function const index, then (isLocal,index)*upvalueCount
  CloseUpvalue,
  Return,
  MakeArray,   // operand: element count
  GetIndex,
  Class,       // operand: name const — push new class
  Inherit,     // stack: [class, superclass] → class
  Method,      // operand: name const, flags — stack: [class, fn] → class
  Field,       // operand: name const, flags — stack: [class, default] → class
  StaticField, // operand: name const — stack: [class, value] → class
  Implement,   // stack: [class, contract] → class (check + record)
  GetProp,     // operand: name const — pop receiver, push field/bound method/static
  SetProp,     // operand: name const — stack: [receiver, value] → value
  Invoke,      // operand: name const, arg count — stack: [receiver, args...]
  SuperInvoke, // operand: name const, arg count — CALL PARENT
  Construct,   // operand: arg count — stack: [class, args...] → instance
  Halt,
};

// Method/Field flags
constexpr uint8_t kFlagPrivate = 1;
constexpr uint8_t kFlagStatic = 2;


struct Chunk {
  std::vector<uint8_t> code;
  std::vector<std::size_t> lines;
  std::vector<struct Value> constants;

  std::size_t addConstant(struct Value value);
  void write(Op op, std::size_t line);
  void writeByte(uint8_t byte, std::size_t line);
  void writeShort(uint16_t value, std::size_t line);
  int writeJump(Op op, std::size_t line);
  void patchJump(int offset);
};

std::string opName(Op op);

}  // namespace luke
