#include "luke/vm.hpp"

#include <cmath>
#include <iostream>

namespace luke {

VM::VM(Heap &heap) : heap_(heap) {
  stack_.reserve(256);
}

void VM::push(Value value) { stack_.push_back(value); }

Value VM::pop() {
  Value v = stack_.back();
  stack_.pop_back();
  return v;
}

Value VM::peek(int distance) const {
  return stack_[stack_.size() - 1 - static_cast<std::size_t>(distance)];
}

void VM::runtimeError(const std::string &message) {
  std::size_t line = 0;
  if (chunk_ && ip_ > 0 && ip_ - 1 < chunk_->lines.size()) {
    line = chunk_->lines[ip_ - 1];
  }
  std::cerr << "Runtime error";
  if (line) std::cerr << " on line " << line;
  std::cerr << ": " << message << "\n";
  hadError_ = true;
  stack_.clear();
}

uint8_t VM::readByte() { return chunk_->code[ip_++]; }

uint16_t VM::readShort() {
  ip_ += 2;
  return static_cast<uint16_t>((chunk_->code[ip_ - 2] << 8) | chunk_->code[ip_ - 1]);
}

Value VM::readConstant() {
  return chunk_->constants[readByte()];
}

InterpretResult VM::interpret(Chunk &chunk) {
  chunk_ = &chunk;
  ip_ = 0;
  hadError_ = false;
  stack_.clear();
  return run();
}

bool VM::binaryArith(Op op) {
  Value b = pop();
  Value a = pop();

  if (op == Op::Add) {
    if (isString(a) || isString(b)) {
      std::string out = a.toString() + b.toString();
      push(Value::object(heap_.allocateString(std::move(out))));
      return true;
    }
  }

  if (!a.isNumber() || !b.isNumber()) {
    runtimeError("Operands must be numbers (or strings for ADD).");
    return false;
  }

  switch (op) {
    case Op::Add:
      push(Value::number(a.as.number + b.as.number));
      break;
    case Op::Subtract:
      push(Value::number(a.as.number - b.as.number));
      break;
    case Op::Multiply:
      push(Value::number(a.as.number * b.as.number));
      break;
    case Op::Divide:
      if (b.as.number == 0.0) {
        runtimeError("Division by zero.");
        return false;
      }
      push(Value::number(a.as.number / b.as.number));
      break;
    default:
      runtimeError("Internal: bad arithmetic op.");
      return false;
  }
  return true;
}

InterpretResult VM::run() {
  auto collectRoots = [this]() {
    heap_.collect([this](std::function<void(Value &)> visit) {
      for (Value &v : stack_) visit(v);
      for (auto &kv : globals_) visit(kv.second);
      if (chunk_) {
        for (Value &c : chunk_->constants) visit(c);
      }
    });
  };

  while (!hadError_) {
    if (heap_.bytesAllocated() > 512 * 1024) {
      collectRoots();
    }

    Op instruction = static_cast<Op>(readByte());
    switch (instruction) {
      case Op::Constant:
        push(readConstant());
        break;
      case Op::Nil:
        push(Value::nil());
        break;
      case Op::True:
        push(Value::boolean(true));
        break;
      case Op::False:
        push(Value::boolean(false));
        break;
      case Op::Pop:
        pop();
        break;
      case Op::GetGlobal: {
        Value nameVal = readConstant();
        if (!isString(nameVal)) {
          runtimeError("Global name must be a string.");
          break;
        }
        auto it = globals_.find(asString(nameVal)->chars);
        if (it == globals_.end()) {
          runtimeError("Undefined variable '" + asString(nameVal)->chars + "'.");
          break;
        }
        push(it->second);
        break;
      }
      case Op::SetGlobal: {
        Value nameVal = readConstant();
        if (!isString(nameVal)) {
          runtimeError("Global name must be a string.");
          break;
        }
        globals_[asString(nameVal)->chars] = peek();
        break;
      }
      case Op::GetLocal: {
        uint8_t slot = readByte();
        if (slot >= stack_.size()) {
          runtimeError("Invalid local slot.");
          break;
        }
        push(stack_[slot]);
        break;
      }
      case Op::SetLocal: {
        uint8_t slot = readByte();
        if (slot >= stack_.size()) {
          runtimeError("Invalid local slot.");
          break;
        }
        stack_[slot] = peek();
        break;
      }
      case Op::Add:
        if (!binaryArith(Op::Add)) return InterpretResult::RuntimeError;
        break;
      case Op::Subtract:
        if (!binaryArith(Op::Subtract)) return InterpretResult::RuntimeError;
        break;
      case Op::Multiply:
        if (!binaryArith(Op::Multiply)) return InterpretResult::RuntimeError;
        break;
      case Op::Divide:
        if (!binaryArith(Op::Divide)) return InterpretResult::RuntimeError;
        break;
      case Op::Negate: {
        Value v = pop();
        if (!v.isNumber()) {
          runtimeError("Operand must be a number.");
          break;
        }
        push(Value::number(-v.as.number));
        break;
      }
      case Op::Not:
        push(Value::boolean(!pop().isTruthy()));
        break;
      case Op::Equal: {
        Value b = pop();
        Value a = pop();
        if (a.isNumber() && b.isNumber()) {
          push(Value::boolean(a.as.number == b.as.number));
        } else if (isString(a) && isString(b)) {
          push(Value::boolean(asString(a)->chars == asString(b)->chars));
        } else if (a.isBool() && b.isBool()) {
          push(Value::boolean(a.as.boolean == b.as.boolean));
        } else if (a.isNil() && b.isNil()) {
          push(Value::boolean(true));
        } else {
          push(Value::boolean(false));
        }
        break;
      }
      case Op::Greater: {
        Value b = pop();
        Value a = pop();
        if (!a.isNumber() || !b.isNumber()) {
          runtimeError("Operands must be numbers.");
          break;
        }
        push(Value::boolean(a.as.number > b.as.number));
        break;
      }
      case Op::Less: {
        Value b = pop();
        Value a = pop();
        if (!a.isNumber() || !b.isNumber()) {
          runtimeError("Operands must be numbers.");
          break;
        }
        push(Value::boolean(a.as.number < b.as.number));
        break;
      }
      case Op::Print:
        std::cout << pop().toString() << "\n";
        break;
      case Op::Jump: {
        uint16_t offset = readShort();
        ip_ += offset;
        break;
      }
      case Op::JumpIfFalse: {
        uint16_t offset = readShort();
        if (!peek().isTruthy()) ip_ += offset;
        break;
      }
      case Op::Loop: {
        uint16_t offset = readShort();
        ip_ -= offset;
        break;
      }
      case Op::MakeArray: {
        uint8_t count = readByte();
        ObjArray *arr = heap_.allocateArray(count);
        arr->items.resize(count);
        for (int i = count - 1; i >= 0; --i) {
          arr->items[static_cast<std::size_t>(i)] = pop();
        }
        push(Value::object(arr));
        break;
      }
      case Op::GetIndex: {
        Value index = pop();
        Value container = pop();
        if (!container.isObj() || container.as.obj->type != ObjType::Array) {
          runtimeError("Can only index arrays.");
          break;
        }
        if (!index.isNumber()) {
          runtimeError("Array index must be a number.");
          break;
        }
        auto *arr = static_cast<ObjArray *>(container.as.obj);
        auto i = static_cast<std::size_t>(index.as.number);
        if (i >= arr->items.size()) {
          runtimeError("Array index out of bounds.");
          break;
        }
        push(arr->items[i]);
        break;
      }
      case Op::Call:
      case Op::Return:
        runtimeError("Functions are not fully wired in this native build yet.");
        break;
      case Op::Halt:
        collectRoots();
        return InterpretResult::Ok;
    }

    if (hadError_) return InterpretResult::RuntimeError;
    if (ip_ >= chunk_->code.size()) {
      collectRoots();
      return InterpretResult::Ok;
    }
  }
  return InterpretResult::RuntimeError;
}

}  // namespace luke
