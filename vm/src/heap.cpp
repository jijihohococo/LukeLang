#include "luke/heap.hpp"
#include "luke/function.hpp"
#include "luke/object.hpp"

namespace luke {

Heap::Heap(std::size_t gcThresholdBytes) : nextGc_(gcThresholdBytes) {}

Heap::~Heap() {
  Obj *obj = objects_;
  while (obj) {
    Obj *next = obj->next;
    freeObject(obj);
    obj = next;
  }
  objects_ = nullptr;
  bytesAllocated_ = 0;
  objectCount_ = 0;
}

void Heap::track(Obj *obj, std::size_t bytes) {
  obj->marked = false;
  obj->next = objects_;
  objects_ = obj;
  bytesAllocated_ += bytes;
  objectCount_ += 1;
  maybeCollect();
}

ObjString *Heap::allocateString(std::string chars) {
  auto *obj = new ObjString();
  obj->type = ObjType::String;
  obj->chars = std::move(chars);
  track(obj, sizeof(ObjString) + obj->chars.size());
  return obj;
}

ObjArray *Heap::allocateArray(std::size_t capacity) {
  auto *obj = new ObjArray();
  obj->type = ObjType::Array;
  obj->items.reserve(capacity);
  track(obj, sizeof(ObjArray) + capacity * sizeof(Value));
  return obj;
}

ObjFunction *Heap::allocateFunction(std::string name, int arity) {
  auto *obj = new ObjFunction();
  obj->type = ObjType::Function;
  obj->name = std::move(name);
  obj->arity = arity;
  track(obj, sizeof(ObjFunction) + obj->name.size());
  return obj;
}

ObjClass *Heap::allocateClass(std::string name) {
  auto *obj = new ObjClass();
  obj->type = ObjType::Class;
  obj->name = std::move(name);
  track(obj, sizeof(ObjClass) + obj->name.size());
  return obj;
}

ObjInstance *Heap::allocateInstance(ObjClass *klass) {
  auto *obj = new ObjInstance();
  obj->type = ObjType::Instance;
  obj->klass = klass;
  track(obj, sizeof(ObjInstance));
  return obj;
}

ObjBoundMethod *Heap::allocateBoundMethod(Value receiver, ObjFunction *method) {
  auto *obj = new ObjBoundMethod();
  obj->type = ObjType::BoundMethod;
  obj->receiver = receiver;
  obj->method = method;
  track(obj, sizeof(ObjBoundMethod));
  return obj;
}

ObjContract *Heap::allocateContract(std::string name) {
  auto *obj = new ObjContract();
  obj->type = ObjType::Contract;
  obj->name = std::move(name);
  track(obj, sizeof(ObjContract) + obj->name.size());
  return obj;
}

ObjClosure *Heap::allocateClosure(ObjFunction *function) {
  auto *obj = new ObjClosure();
  obj->type = ObjType::Closure;
  obj->function = function;
  obj->upvalues.resize(function ? function->upvalueDescs.size() : 0, nullptr);
  track(obj, sizeof(ObjClosure) + obj->upvalues.size() * sizeof(ObjUpvalue *));
  return obj;
}

ObjUpvalue *Heap::allocateUpvalue(Value *slot) {
  auto *obj = new ObjUpvalue();
  obj->type = ObjType::Upvalue;
  obj->location = slot;
  track(obj, sizeof(ObjUpvalue));
  return obj;
}

void Heap::maybeCollect() {
  if (collecting_) return;
  if (bytesAllocated_ < nextGc_) return;
  nextGc_ = bytesAllocated_ + (bytesAllocated_ / 2) + 1024;
}

void Heap::collect(const std::vector<Value *> &roots) {
  collect([&](std::function<void(Value &)> visit) {
    for (Value *root : roots) {
      if (root) visit(*root);
    }
  });
}

void Heap::collect(const std::function<void(std::function<void(Value &)>)> &traceRoots) {
  if (collecting_) return;
  collecting_ = true;
  collections_ += 1;
  traceRoots([this](Value &v) { markValue(v); });
  sweep();
  nextGc_ = bytesAllocated_ * 2;
  if (nextGc_ < 64 * 1024) nextGc_ = 64 * 1024;
  collecting_ = false;
}

void Heap::markValue(Value &v) {
  if (v.isObj()) markObject(v.as.obj);
}

void Heap::markObject(Obj *obj) {
  if (!obj || obj->marked) return;
  obj->marked = true;
  switch (obj->type) {
    case ObjType::String:
      break;
    case ObjType::Array: {
      auto *arr = static_cast<ObjArray *>(obj);
      for (Value &item : arr->items) markValue(item);
      break;
    }
    case ObjType::Function: {
      auto *fn = static_cast<ObjFunction *>(obj);
      for (Value &c : fn->chunk.constants) markValue(c);
      if (fn->klass) markObject(fn->klass);
      break;
    }
    case ObjType::Class: {
      auto *klass = static_cast<ObjClass *>(obj);
      if (klass->superclass) markObject(klass->superclass);
      for (auto &kv : klass->methods) markObject(kv.second);
      for (auto &field : klass->fields) markValue(field.defaultValue);
      for (auto &kv : klass->statics) markValue(kv.second);
      for (auto *c : klass->contracts) markObject(c);
      break;
    }
    case ObjType::Instance: {
      auto *inst = static_cast<ObjInstance *>(obj);
      if (inst->klass) markObject(inst->klass);
      for (auto &kv : inst->fields) markValue(kv.second);
      break;
    }
    case ObjType::BoundMethod: {
      auto *bound = static_cast<ObjBoundMethod *>(obj);
      markValue(bound->receiver);
      if (bound->method) markObject(bound->method);
      break;
    }
    case ObjType::Contract:
      break;
    case ObjType::Closure: {
      auto *cl = static_cast<ObjClosure *>(obj);
      if (cl->function) markObject(cl->function);
      for (auto *uv : cl->upvalues) {
        if (uv) markObject(uv);
      }
      break;
    }
    case ObjType::Upvalue: {
      auto *uv = static_cast<ObjUpvalue *>(obj);
      markValue(uv->closed);
      break;
    }
  }
}

void Heap::sweep() {
  Obj **cursor = &objects_;
  while (*cursor) {
    Obj *obj = *cursor;
    if (!obj->marked) {
      *cursor = obj->next;
      freeObject(obj);
      objectCount_ -= 1;
    } else {
      obj->marked = false;
      cursor = &obj->next;
    }
  }
}

void Heap::freeObject(Obj *obj) {
  std::size_t bytes = 0;
  switch (obj->type) {
    case ObjType::String: {
      auto *s = static_cast<ObjString *>(obj);
      bytes = sizeof(ObjString) + s->chars.size();
      delete s;
      break;
    }
    case ObjType::Array: {
      auto *a = static_cast<ObjArray *>(obj);
      bytes = sizeof(ObjArray) + a->items.capacity() * sizeof(Value);
      delete a;
      break;
    }
    case ObjType::Function: {
      auto *f = static_cast<ObjFunction *>(obj);
      bytes = sizeof(ObjFunction) + f->name.size();
      delete f;
      break;
    }
    case ObjType::Class: {
      auto *c = static_cast<ObjClass *>(obj);
      bytes = sizeof(ObjClass) + c->name.size();
      delete c;
      break;
    }
    case ObjType::Instance:
      bytes = sizeof(ObjInstance);
      delete static_cast<ObjInstance *>(obj);
      break;
    case ObjType::BoundMethod:
      bytes = sizeof(ObjBoundMethod);
      delete static_cast<ObjBoundMethod *>(obj);
      break;
    case ObjType::Contract: {
      auto *c = static_cast<ObjContract *>(obj);
      bytes = sizeof(ObjContract) + c->name.size();
      delete c;
      break;
    }
    case ObjType::Closure: {
      auto *c = static_cast<ObjClosure *>(obj);
      bytes = sizeof(ObjClosure) + c->upvalues.size() * sizeof(ObjUpvalue *);
      delete c;
      break;
    }
    case ObjType::Upvalue:
      bytes = sizeof(ObjUpvalue);
      delete static_cast<ObjUpvalue *>(obj);
      break;
  }
  if (bytesAllocated_ >= bytes) bytesAllocated_ -= bytes;
  else bytesAllocated_ = 0;
}

}  // namespace luke
