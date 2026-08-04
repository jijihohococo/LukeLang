#include "luke/vm.hpp"

#include <iostream>

namespace luke {

VM::VM(Heap &heap) : heap_(heap) {
  stack_.reserve(256);
  frames_.reserve(kFramesMax);
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

CallFrame &VM::frame() { return frames_.back(); }

void VM::runtimeError(const std::string &message) {
  std::size_t line = 0;
  if (!frames_.empty()) {
    CallFrame &f = frame();
    if (f.chunk && f.ip > 0 && f.ip - 1 < f.chunk->lines.size()) {
      line = f.chunk->lines[f.ip - 1];
    }
  }
  std::cerr << "Runtime error";
  if (line) std::cerr << " on line " << line;
  std::cerr << ": " << message << "\n";

  for (int i = static_cast<int>(frames_.size()) - 1; i >= 0; --i) {
    CallFrame &f = frames_[static_cast<std::size_t>(i)];
    std::string name = f.function ? f.function->name : "<script>";
    std::size_t lineNo = 0;
    if (f.chunk && f.ip > 0 && f.ip - 1 < f.chunk->lines.size()) {
      lineNo = f.chunk->lines[f.ip - 1];
    }
    std::cerr << "  in " << name;
    if (lineNo) std::cerr << " at line " << lineNo;
    std::cerr << "\n";
  }

  hadError_ = true;
  stack_.clear();
  frames_.clear();
}

uint8_t VM::readByte() { return frame().chunk->code[frame().ip++]; }

uint16_t VM::readShort() {
  frame().ip += 2;
  auto &code = frame().chunk->code;
  auto ip = frame().ip;
  return static_cast<uint16_t>((code[ip - 2] << 8) | code[ip - 1]);
}

Value VM::readConstant() { return frame().chunk->constants[readByte()]; }

InterpretResult VM::interpret(Chunk &chunk) {
  scriptChunk_ = &chunk;
  hadError_ = false;
  stack_.clear();
  frames_.clear();

  CallFrame script;
  script.function = nullptr;
  script.chunk = &chunk;
  script.ip = 0;
  script.slots = 0;
  frames_.push_back(script);

  push(Value::nil());
  return run();
}

bool VM::call(ObjFunction *function, int argCount) {
  if (argCount != function->arity) {
    runtimeError("Expected " + std::to_string(function->arity) + " arguments but got " +
                 std::to_string(argCount) + ".");
    return false;
  }
  if (static_cast<int>(frames_.size()) >= kFramesMax) {
    runtimeError("Stack overflow.");
    return false;
  }

  CallFrame f;
  f.function = function;
  f.chunk = &function->chunk;
  f.ip = 0;
  f.slots = stack_.size() - static_cast<std::size_t>(argCount) - 1;
  frames_.push_back(f);
  return true;
}

bool VM::invokeFromClass(ObjClass *klass, const std::string &name, int argCount) {
  ObjFunction *method = klass->findMethod(name);
  if (!method) {
    runtimeError("Undefined method '" + name + "'.");
    return false;
  }
  return call(method, argCount);
}

bool VM::bindMethod(ObjClass *klass, const std::string &name) {
  ObjFunction *method = klass->findMethod(name);
  if (!method) return false;
  Value receiver = peek();
  pop();
  push(Value::object(heap_.allocateBoundMethod(receiver, method)));
  return true;
}

void VM::applyFieldDefaults(ObjInstance *instance, ObjClass *klass) {
  if (!klass) return;
  applyFieldDefaults(instance, klass->superclass);
  for (auto &field : klass->fields) {
    instance->fields[field.first] = field.second;
  }
}

bool VM::callValue(Value callee, int argCount) {
  if (isFunction(callee)) {
    return call(asFunction(callee), argCount);
  }
  if (isBoundMethod(callee)) {
    auto *bound = asBoundMethod(callee);
    // Replace bound method with receiver so slot 0 is SELF.
    stack_[stack_.size() - static_cast<std::size_t>(argCount) - 1] = bound->receiver;
    return call(bound->method, argCount);
  }
  if (isClass(callee)) {
    // Treat Call on class as Construct.
    auto *klass = asClass(callee);
    ObjInstance *instance = heap_.allocateInstance(klass);
    applyFieldDefaults(instance, klass);
    stack_[stack_.size() - static_cast<std::size_t>(argCount) - 1] = Value::object(instance);
    ObjFunction *born = klass->findMethod("born");
    if (born) {
      return call(born, argCount);
    }
    if (argCount != 0) {
      runtimeError("Class has no WHEN BORN but arguments were given.");
      return false;
    }
    return true;
  }
  runtimeError("Can only call functions and blueprints.");
  return false;
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
      if (scriptChunk_) {
        for (Value &c : scriptChunk_->constants) visit(c);
      }
      for (CallFrame &f : frames_) {
        if (f.function) {
          for (Value &c : f.function->chunk.constants) visit(c);
        }
      }
    });
  };

  while (!hadError_) {
    if (heap_.bytesAllocated() > 512 * 1024) {
      collectRoots();
    }

    if (frames_.empty()) {
      collectRoots();
      return InterpretResult::Ok;
    }

    if (frame().ip >= frame().chunk->code.size()) {
      if (frames_.size() == 1) {
        collectRoots();
        return InterpretResult::Ok;
      }
      // Falling off a method/constructor without return:
      // constructors should yield the instance (slot 0).
      Value result = Value::nil();
      if (frame().function && frame().function->isMethod &&
          frame().function->name == "born") {
        result = stack_[frame().slots];
      }
      std::size_t slots = frame().slots;
      frames_.pop_back();
      stack_.resize(slots);
      push(result);
      continue;
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
        push(stack_[frame().slots + slot]);
        break;
      }
      case Op::SetLocal: {
        uint8_t slot = readByte();
        stack_[frame().slots + slot] = peek();
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
        frame().ip += offset;
        break;
      }
      case Op::JumpIfFalse: {
        uint16_t offset = readShort();
        if (!peek().isTruthy()) frame().ip += offset;
        break;
      }
      case Op::Loop: {
        uint16_t offset = readShort();
        frame().ip -= offset;
        break;
      }
      case Op::Call: {
        int argCount = readByte();
        if (!callValue(peek(argCount), argCount)) {
          return InterpretResult::RuntimeError;
        }
        break;
      }
      case Op::Return: {
        Value result = pop();
        // Constructor return always yields the instance.
        if (frame().function && frame().function->isMethod &&
            frame().function->name == "born") {
          result = stack_[frame().slots];
        }
        std::size_t slots = frame().slots;
        frames_.pop_back();
        if (frames_.empty()) {
          collectRoots();
          return InterpretResult::Ok;
        }
        stack_.resize(slots);
        push(result);
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
      case Op::Class: {
        Value nameVal = readConstant();
        if (!isString(nameVal)) {
          runtimeError("Class name must be a string.");
          break;
        }
        push(Value::object(heap_.allocateClass(asString(nameVal)->chars)));
        break;
      }
      case Op::Inherit: {
        Value superclassVal = pop();
        Value klassVal = peek();
        if (!isClass(superclassVal)) {
          runtimeError("Superclass must be a blueprint.");
          break;
        }
        if (!isClass(klassVal)) {
          runtimeError("Can only inherit on a blueprint.");
          break;
        }
        asClass(klassVal)->superclass = asClass(superclassVal);
        break;
      }
      case Op::Method: {
        Value nameVal = readConstant();
        Value methodVal = pop();
        Value klassVal = peek();
        if (!isString(nameVal) || !isFunction(methodVal) || !isClass(klassVal)) {
          runtimeError("Invalid METHOD binding.");
          break;
        }
        auto *fn = asFunction(methodVal);
        auto *klass = asClass(klassVal);
        fn->klass = klass;
        fn->isMethod = true;
        klass->methods[asString(nameVal)->chars] = fn;
        break;
      }
      case Op::Field: {
        Value nameVal = readConstant();
        Value defaultVal = pop();
        Value klassVal = peek();
        if (!isString(nameVal) || !isClass(klassVal)) {
          runtimeError("Invalid HAS field binding.");
          break;
        }
        asClass(klassVal)->fields.emplace_back(asString(nameVal)->chars, defaultVal);
        break;
      }
      case Op::GetProp: {
        Value nameVal = readConstant();
        Value object = pop();
        if (!isString(nameVal)) {
          runtimeError("Property name must be a string.");
          break;
        }
        std::string name = asString(nameVal)->chars;
        if (isInstance(object)) {
          auto *inst = asInstance(object);
          auto it = inst->fields.find(name);
          if (it != inst->fields.end()) {
            push(it->second);
            break;
          }
          push(object);
          if (!bindMethod(inst->klass, name)) {
            pop();
            runtimeError("Undefined property '" + name + "'.");
          }
          break;
        }
        runtimeError("Only instances have properties.");
        break;
      }
      case Op::SetProp: {
        Value nameVal = readConstant();
        Value value = pop();
        Value object = pop();
        if (!isString(nameVal)) {
          runtimeError("Property name must be a string.");
          break;
        }
        if (!isInstance(object)) {
          runtimeError("Only instances have fields.");
          break;
        }
        asInstance(object)->fields[asString(nameVal)->chars] = value;
        push(value);
        break;
      }
      case Op::Invoke: {
        Value nameVal = readConstant();
        int argCount = readByte();
        if (!isString(nameVal)) {
          runtimeError("Method name must be a string.");
          break;
        }
        Value receiver = peek(argCount);
        if (!isInstance(receiver)) {
          runtimeError("Only instances have methods.");
          break;
        }
        if (!invokeFromClass(asInstance(receiver)->klass, asString(nameVal)->chars, argCount)) {
          return InterpretResult::RuntimeError;
        }
        break;
      }
      case Op::SuperInvoke: {
        Value nameVal = readConstant();
        int argCount = readByte();
        if (!isString(nameVal)) {
          runtimeError("Method name must be a string.");
          break;
        }
        if (!frame().function || !frame().function->klass ||
            !frame().function->klass->superclass) {
          runtimeError("CALL PARENT used without a parent blueprint.");
          break;
        }
        // Receiver is already at peek(argCount) — compiler pushes SELF then args.
        if (!invokeFromClass(frame().function->klass->superclass, asString(nameVal)->chars,
                             argCount)) {
          return InterpretResult::RuntimeError;
        }
        break;
      }
      case Op::Construct: {
        int argCount = readByte();
        Value klassVal = peek(argCount);
        if (!isClass(klassVal)) {
          runtimeError("NEW expects a blueprint.");
          break;
        }
        if (!callValue(klassVal, argCount)) {
          return InterpretResult::RuntimeError;
        }
        break;
      }
      case Op::Halt:
        collectRoots();
        return InterpretResult::Ok;
    }

    if (hadError_) return InterpretResult::RuntimeError;
  }
  return InterpretResult::RuntimeError;
}

}  // namespace luke
