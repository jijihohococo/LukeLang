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
  openUpvalues_ = nullptr;
}

uint8_t VM::readByte() { return frame().chunk->code[frame().ip++]; }

uint16_t VM::readShort() {
  frame().ip += 2;
  auto &code = frame().chunk->code;
  auto ip = frame().ip;
  return static_cast<uint16_t>((code[ip - 2] << 8) | code[ip - 1]);
}

Value VM::readConstant() { return frame().chunk->constants[readByte()]; }

bool VM::canAccessPrivate(ObjClass *owner) const {
  if (frames_.empty() || !owner) return false;
  const CallFrame &f = frames_.back();
  return f.function && f.function->isMethod && f.function->klass == owner;
}

InterpretResult VM::interpret(Chunk &chunk) {
  scriptChunk_ = &chunk;
  hadError_ = false;
  stack_.clear();
  frames_.clear();
  openUpvalues_ = nullptr;

  CallFrame script;
  script.closure = nullptr;
  script.function = nullptr;
  script.chunk = &chunk;
  script.ip = 0;
  script.slots = 0;
  frames_.push_back(script);

  push(Value::nil());
  return run();
}

ObjUpvalue *VM::captureUpvalue(Value *local) {
  ObjUpvalue *prev = nullptr;
  ObjUpvalue *uv = openUpvalues_;
  while (uv && uv->location > local) {
    prev = uv;
    uv = uv->next;
  }
  if (uv && uv->location == local) return uv;

  ObjUpvalue *created = heap_.allocateUpvalue(local);
  created->next = uv;
  if (prev) prev->next = created;
  else openUpvalues_ = created;
  return created;
}

void VM::closeUpvalues(Value *last) {
  while (openUpvalues_ && openUpvalues_->location >= last) {
    ObjUpvalue *uv = openUpvalues_;
    uv->closed = *uv->location;
    uv->location = &uv->closed;
    openUpvalues_ = uv->next;
  }
}

bool VM::call(ObjFunction *function, ObjClosure *closure, int argCount) {
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
  f.closure = closure;
  f.chunk = &function->chunk;
  f.ip = 0;
  f.slots = stack_.size() - static_cast<std::size_t>(argCount) - 1;
  frames_.push_back(f);
  return true;
}

bool VM::invokeFromClass(ObjClass *klass, const std::string &name, int argCount,
                         bool allowPrivate) {
  ObjFunction *method = klass->findMethod(name);
  if (!method) {
    runtimeError("Undefined method '" + name + "'.");
    return false;
  }
  if (method->isPrivate && !allowPrivate) {
    ObjClass *owner = klass->methodOwner(name);
    if (!canAccessPrivate(owner)) {
      runtimeError("Method '" + name + "' is private.");
      return false;
    }
  }
  // Static methods: receiver should be the class; instance methods need instance.
  if (method->isStatic) {
    Value recv = peek(argCount);
    if (!isClass(recv)) {
      // Allow ASK instance TO static? No — require class receiver.
      runtimeError("Static method '" + name + "' must be called on the blueprint.");
      return false;
    }
  }
  return call(method, nullptr, argCount);
}

bool VM::bindMethod(ObjClass *klass, const std::string &name, bool allowPrivate) {
  ObjFunction *method = klass->findMethod(name);
  if (!method) return false;
  if (method->isPrivate && !allowPrivate) {
    ObjClass *owner = klass->methodOwner(name);
    if (!canAccessPrivate(owner)) return false;
  }
  Value receiver = peek();
  pop();
  push(Value::object(heap_.allocateBoundMethod(receiver, method)));
  return true;
}

void VM::applyFieldDefaults(ObjInstance *instance, ObjClass *klass) {
  if (!klass) return;
  applyFieldDefaults(instance, klass->superclass);
  for (auto &field : klass->fields) {
    instance->fields[field.name] = field.defaultValue;
  }
}

bool VM::callValue(Value callee, int argCount) {
  if (isClosure(callee)) {
    auto *cl = asClosure(callee);
    return call(cl->function, cl, argCount);
  }
  if (isFunction(callee)) {
    return call(asFunction(callee), nullptr, argCount);
  }
  if (isBoundMethod(callee)) {
    auto *bound = asBoundMethod(callee);
    stack_[stack_.size() - static_cast<std::size_t>(argCount) - 1] = bound->receiver;
    return call(bound->method, nullptr, argCount);
  }
  if (isClass(callee)) {
    auto *klass = asClass(callee);
    ObjInstance *instance = heap_.allocateInstance(klass);
    applyFieldDefaults(instance, klass);
    stack_[stack_.size() - static_cast<std::size_t>(argCount) - 1] = Value::object(instance);
    ObjFunction *born = klass->findMethod("born");
    if (born) {
      return call(born, nullptr, argCount);
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
      push(Value::object(heap_.allocateString(a.toString() + b.toString())));
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
        if (f.closure) {
          Value tmp = Value::object(f.closure);
          visit(tmp);
        }
      }
      for (ObjUpvalue *uv = openUpvalues_; uv; uv = uv->next) {
        Value tmp = Value::object(uv);
        visit(tmp);
      }
    });
  };

  while (!hadError_) {
    if (heap_.bytesAllocated() > 512 * 1024) collectRoots();

    if (frames_.empty()) {
      collectRoots();
      return InterpretResult::Ok;
    }

    if (frame().ip >= frame().chunk->code.size()) {
      if (frames_.size() == 1) {
        collectRoots();
        return InterpretResult::Ok;
      }
      Value result = Value::nil();
      if (frame().function && frame().function->isMethod && frame().function->name == "born") {
        result = stack_[frame().slots];
      }
      closeUpvalues(&stack_[frame().slots]);
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
      case Op::GetUpvalue: {
        uint8_t slot = readByte();
        if (!frame().closure || slot >= frame().closure->upvalues.size()) {
          runtimeError("Invalid upvalue.");
          break;
        }
        push(*frame().closure->upvalues[slot]->location);
        break;
      }
      case Op::SetUpvalue: {
        uint8_t slot = readByte();
        if (!frame().closure || slot >= frame().closure->upvalues.size()) {
          runtimeError("Invalid upvalue.");
          break;
        }
        *frame().closure->upvalues[slot]->location = peek();
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
        if (a.isNumber() && b.isNumber())
          push(Value::boolean(a.as.number == b.as.number));
        else if (isString(a) && isString(b))
          push(Value::boolean(asString(a)->chars == asString(b)->chars));
        else if (a.isBool() && b.isBool())
          push(Value::boolean(a.as.boolean == b.as.boolean));
        else if (a.isNil() && b.isNil())
          push(Value::boolean(true));
        else
          push(Value::boolean(false));
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
      case Op::Jump:
        frame().ip += readShort();
        break;
      case Op::JumpIfFalse: {
        uint16_t offset = readShort();
        if (!peek().isTruthy()) frame().ip += offset;
        break;
      }
      case Op::Loop:
        frame().ip -= readShort();
        break;
      case Op::Call: {
        int argCount = readByte();
        if (!callValue(peek(argCount), argCount)) return InterpretResult::RuntimeError;
        break;
      }
      case Op::Closure: {
        Value fnVal = readConstant();
        if (!isFunction(fnVal)) {
          runtimeError("CLOSURE expects a function.");
          break;
        }
        auto *fn = asFunction(fnVal);
        ObjClosure *closure = heap_.allocateClosure(fn);
        for (std::size_t i = 0; i < fn->upvalueDescs.size(); ++i) {
          auto &desc = fn->upvalueDescs[i];
          if (desc.isLocal) {
            closure->upvalues[i] = captureUpvalue(&stack_[frame().slots + desc.index]);
          } else {
            closure->upvalues[i] = frame().closure->upvalues[desc.index];
          }
        }
        push(Value::object(closure));
        break;
      }
      case Op::CloseUpvalue:
        closeUpvalues(&stack_.back());
        pop();
        break;
      case Op::Return: {
        Value result = pop();
        if (frame().function && frame().function->isMethod && frame().function->name == "born") {
          result = stack_[frame().slots];
        }
        closeUpvalues(&stack_[frame().slots]);
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
        if (!isInstance(container) &&
            !(container.isObj() && container.as.obj->type == ObjType::Array)) {
          runtimeError("Can only index arrays.");
          break;
        }
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
        push(Value::object(heap_.allocateClass(asString(nameVal)->chars)));
        break;
      }
      case Op::Inherit: {
        Value superclassVal = pop();
        Value klassVal = peek();
        if (!isClass(superclassVal) || !isClass(klassVal)) {
          runtimeError("Invalid inheritance.");
          break;
        }
        asClass(klassVal)->superclass = asClass(superclassVal);
        break;
      }
      case Op::Method: {
        Value nameVal = readConstant();
        uint8_t flags = readByte();
        Value methodVal = pop();
        Value klassVal = peek();
        if (!isFunction(methodVal) || !isClass(klassVal)) {
          runtimeError("Invalid METHOD binding.");
          break;
        }
        auto *fn = asFunction(methodVal);
        auto *klass = asClass(klassVal);
        fn->klass = klass;
        fn->isMethod = true;
        fn->isStatic = (flags & kFlagStatic) != 0;
        fn->isPrivate = (flags & kFlagPrivate) != 0;
        std::string name = asString(nameVal)->chars;
        klass->methods[name] = fn;
        if (fn->isPrivate) klass->privateMethods[name] = true;
        break;
      }
      case Op::Field: {
        Value nameVal = readConstant();
        uint8_t flags = readByte();
        Value defaultVal = pop();
        Value klassVal = peek();
        if (!isClass(klassVal)) {
          runtimeError("Invalid HAS field binding.");
          break;
        }
        auto *klass = asClass(klassVal);
        FieldInfo info;
        info.name = asString(nameVal)->chars;
        info.defaultValue = defaultVal;
        info.isPrivate = (flags & kFlagPrivate) != 0;
        info.owner = klass;
        klass->fields.push_back(info);
        break;
      }
      case Op::StaticField: {
        Value nameVal = readConstant();
        Value value = pop();
        Value klassVal = peek();
        if (!isClass(klassVal)) {
          runtimeError("Invalid ALWAYS HAS binding.");
          break;
        }
        asClass(klassVal)->statics[asString(nameVal)->chars] = value;
        break;
      }
      case Op::Implement: {
        Value contractVal = pop();
        Value klassVal = peek();
        if (!isContract(contractVal) || !isClass(klassVal)) {
          runtimeError("IMPLEMENTS expects a contract and blueprint.");
          break;
        }
        auto *klass = asClass(klassVal);
        auto *contract = asContract(contractVal);
        for (auto &req : contract->requirements) {
          ObjFunction *m = klass->findMethod(req.first);
          if (!m) {
            runtimeError("Blueprint '" + klass->name + "' does not implement contract '" +
                         contract->name + "' (missing method '" + req.first + "').");
            break;
          }
          if (req.second >= 0 && m->arity != req.second) {
            runtimeError("Blueprint '" + klass->name + "' method '" + req.first +
                         "' has wrong arity for contract '" + contract->name + "'.");
            break;
          }
        }
        if (!hadError_) klass->contracts.push_back(contract);
        break;
      }
      case Op::GetProp: {
        Value nameVal = readConstant();
        Value object = pop();
        std::string name = asString(nameVal)->chars;

        if (isInstance(object)) {
          auto *inst = asInstance(object);
          FieldInfo *info = inst->klass ? inst->klass->findField(name) : nullptr;
          auto it = inst->fields.find(name);
          if (it != inst->fields.end()) {
            if (info && info->isPrivate && !canAccessPrivate(info->owner)) {
              runtimeError("Field '" + name + "' is private.");
              break;
            }
            push(it->second);
            break;
          }
          push(object);
          if (!bindMethod(inst->klass, name, /*allowPrivate=*/false)) {
            // Retry private bind if caller is allowed
            pop();
            push(object);
            if (!bindMethod(inst->klass, name, /*allowPrivate=*/true) ||
                !canAccessPrivate(inst->klass->methodOwner(name))) {
              pop();
              runtimeError("Undefined property '" + name + "'.");
            }
          }
          break;
        }

        if (isClass(object)) {
          auto *klass = asClass(object);
          auto sit = klass->statics.find(name);
          if (sit != klass->statics.end()) {
            push(sit->second);
            break;
          }
          // Walk superclass statics
          bool found = false;
          for (ObjClass *c = klass; c; c = c->superclass) {
            auto it = c->statics.find(name);
            if (it != c->statics.end()) {
              push(it->second);
              found = true;
              break;
            }
          }
          if (found) break;
          push(object);
          if (!bindMethod(klass, name, false)) {
            pop();
            runtimeError("Undefined static property '" + name + "'.");
          }
          break;
        }

        runtimeError("Only instances and blueprints have properties.");
        break;
      }
      case Op::SetProp: {
        Value nameVal = readConstant();
        Value value = pop();
        Value object = pop();
        std::string name = asString(nameVal)->chars;

        if (isInstance(object)) {
          auto *inst = asInstance(object);
          FieldInfo *info = inst->klass ? inst->klass->findField(name) : nullptr;
          if (info && info->isPrivate && !canAccessPrivate(info->owner)) {
            runtimeError("Field '" + name + "' is private.");
            break;
          }
          inst->fields[name] = value;
          push(value);
          break;
        }

        if (isClass(object)) {
          asClass(object)->statics[name] = value;
          push(value);
          break;
        }

        runtimeError("Only instances and blueprints have fields.");
        break;
      }
      case Op::Invoke: {
        Value nameVal = readConstant();
        int argCount = readByte();
        std::string name = asString(nameVal)->chars;
        Value receiver = peek(argCount);

        if (isInstance(receiver)) {
          auto *inst = asInstance(receiver);
          // Prefer instance fields that are callables? Skip — methods only.
          bool allowPrivate = false;
          ObjFunction *m = inst->klass->findMethod(name);
          if (m && m->isPrivate) {
            allowPrivate = canAccessPrivate(inst->klass->methodOwner(name));
          }
          if (!invokeFromClass(inst->klass, name, argCount, allowPrivate)) {
            return InterpretResult::RuntimeError;
          }
          break;
        }

        if (isClass(receiver)) {
          if (!invokeFromClass(asClass(receiver), name, argCount, false)) {
            return InterpretResult::RuntimeError;
          }
          break;
        }

        runtimeError("Only instances and blueprints have methods.");
        break;
      }
      case Op::SuperInvoke: {
        Value nameVal = readConstant();
        int argCount = readByte();
        if (!frame().function || !frame().function->klass ||
            !frame().function->klass->superclass) {
          runtimeError("CALL PARENT used without a parent blueprint.");
          break;
        }
        if (!invokeFromClass(frame().function->klass->superclass, asString(nameVal)->chars,
                             argCount, true)) {
          return InterpretResult::RuntimeError;
        }
        break;
      }
      case Op::Construct: {
        int argCount = readByte();
        if (!callValue(peek(argCount), argCount)) return InterpretResult::RuntimeError;
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
