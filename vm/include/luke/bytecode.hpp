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
  Return,
  MakeArray,   // operand: element count
  GetIndex,
  Halt,
};

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
