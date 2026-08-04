#include "luke/bytecode.hpp"
#include "luke/value.hpp"

namespace luke {

std::size_t Chunk::addConstant(Value value) {
  constants.push_back(value);
  return constants.size() - 1;
}

void Chunk::write(Op op, std::size_t line) {
  writeByte(static_cast<uint8_t>(op), line);
}

void Chunk::writeByte(uint8_t byte, std::size_t line) {
  code.push_back(byte);
  lines.push_back(line);
}

void Chunk::writeShort(uint16_t value, std::size_t line) {
  writeByte(static_cast<uint8_t>((value >> 8) & 0xff), line);
  writeByte(static_cast<uint8_t>(value & 0xff), line);
}

int Chunk::writeJump(Op op, std::size_t line) {
  write(op, line);
  writeByte(0xff, line);
  writeByte(0xff, line);
  return static_cast<int>(code.size() - 2);
}

void Chunk::patchJump(int offset) {
  int jump = static_cast<int>(code.size()) - offset - 2;
  if (jump > 0xffff) jump = 0xffff;
  code[static_cast<std::size_t>(offset)] = static_cast<uint8_t>((jump >> 8) & 0xff);
  code[static_cast<std::size_t>(offset) + 1] = static_cast<uint8_t>(jump & 0xff);
}

std::string opName(Op op) {
  switch (op) {
    case Op::Constant: return "CONSTANT";
    case Op::Nil: return "NIL";
    case Op::True: return "TRUE";
    case Op::False: return "FALSE";
    case Op::Pop: return "POP";
    case Op::GetGlobal: return "GET_GLOBAL";
    case Op::SetGlobal: return "SET_GLOBAL";
    case Op::GetLocal: return "GET_LOCAL";
    case Op::SetLocal: return "SET_LOCAL";
    case Op::Add: return "ADD";
    case Op::Subtract: return "SUBTRACT";
    case Op::Multiply: return "MULTIPLY";
    case Op::Divide: return "DIVIDE";
    case Op::Negate: return "NEGATE";
    case Op::Not: return "NOT";
    case Op::Equal: return "EQUAL";
    case Op::Greater: return "GREATER";
    case Op::Less: return "LESS";
    case Op::Print: return "PRINT";
    case Op::Jump: return "JUMP";
    case Op::JumpIfFalse: return "JUMP_IF_FALSE";
    case Op::Loop: return "LOOP";
    case Op::Call: return "CALL";
    case Op::Return: return "RETURN";
    case Op::MakeArray: return "MAKE_ARRAY";
    case Op::GetIndex: return "GET_INDEX";
    case Op::Class: return "CLASS";
    case Op::Inherit: return "INHERIT";
    case Op::Method: return "METHOD";
    case Op::Field: return "FIELD";
    case Op::GetProp: return "GET_PROP";
    case Op::SetProp: return "SET_PROP";
    case Op::Invoke: return "INVOKE";
    case Op::SuperInvoke: return "SUPER_INVOKE";
    case Op::Construct: return "CONSTRUCT";
    case Op::Halt: return "HALT";
  }
  return "UNKNOWN";
}

}  // namespace luke
