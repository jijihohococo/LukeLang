#pragma once

#include "luke/function.hpp"
#include "luke/value.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace luke {

struct ObjClass;
struct ObjInstance;
struct ObjBoundMethod;
struct ObjContract;
struct ObjClosure;
struct ObjUpvalue;

struct FieldInfo {
  std::string name;
  Value defaultValue;
  bool isPrivate = false;
  ObjClass *owner = nullptr;  // defining class (for private access checks)
};

struct ObjContract : Obj {
  std::string name;
  // required method name → arity (-1 = any)
  std::unordered_map<std::string, int> requirements;
};

struct ObjClass : Obj {
  std::string name;
  ObjClass *superclass = nullptr;
  std::unordered_map<std::string, ObjFunction *> methods;
  std::unordered_map<std::string, bool> privateMethods;  // name → true if SECRET
  std::vector<FieldInfo> fields;
  std::unordered_map<std::string, Value> statics;
  std::vector<ObjContract *> contracts;

  ObjFunction *findMethod(const std::string &name) const {
    for (const ObjClass *c = this; c; c = c->superclass) {
      auto it = c->methods.find(name);
      if (it != c->methods.end()) return it->second;
    }
    return nullptr;
  }

  bool isMethodPrivate(const std::string &name) const {
    for (const ObjClass *c = this; c; c = c->superclass) {
      auto it = c->privateMethods.find(name);
      if (it != c->privateMethods.end()) return it->second;
      if (c->methods.count(name)) return false;
    }
    return false;
  }

  ObjClass *methodOwner(const std::string &name) const {
    for (ObjClass *c = const_cast<ObjClass *>(this); c; c = c->superclass) {
      if (c->methods.count(name)) return c;
    }
    return nullptr;
  }

  FieldInfo *findField(const std::string &name) {
    for (ObjClass *c = this; c; c = c->superclass) {
      for (auto &f : c->fields) {
        if (f.name == name) return &f;
      }
    }
    return nullptr;
  }
};

struct ObjInstance : Obj {
  ObjClass *klass = nullptr;
  std::unordered_map<std::string, Value> fields;
};

struct ObjBoundMethod : Obj {
  Value receiver;
  ObjFunction *method = nullptr;
};

struct ObjUpvalue : Obj {
  Value *location = nullptr;
  Value closed = Value::nil();
  ObjUpvalue *next = nullptr;
};

struct ObjClosure : Obj {
  ObjFunction *function = nullptr;
  std::vector<ObjUpvalue *> upvalues;
};

inline bool isClass(Value v) {
  return v.isObj() && v.as.obj && v.as.obj->type == ObjType::Class;
}
inline bool isInstance(Value v) {
  return v.isObj() && v.as.obj && v.as.obj->type == ObjType::Instance;
}
inline bool isBoundMethod(Value v) {
  return v.isObj() && v.as.obj && v.as.obj->type == ObjType::BoundMethod;
}
inline bool isContract(Value v) {
  return v.isObj() && v.as.obj && v.as.obj->type == ObjType::Contract;
}
inline bool isClosure(Value v) {
  return v.isObj() && v.as.obj && v.as.obj->type == ObjType::Closure;
}
inline bool isUpvalue(Value v) {
  return v.isObj() && v.as.obj && v.as.obj->type == ObjType::Upvalue;
}

inline ObjClass *asClass(Value v) { return static_cast<ObjClass *>(v.as.obj); }
inline ObjInstance *asInstance(Value v) { return static_cast<ObjInstance *>(v.as.obj); }
inline ObjBoundMethod *asBoundMethod(Value v) {
  return static_cast<ObjBoundMethod *>(v.as.obj);
}
inline ObjContract *asContract(Value v) { return static_cast<ObjContract *>(v.as.obj); }
inline ObjClosure *asClosure(Value v) { return static_cast<ObjClosure *>(v.as.obj); }
inline ObjUpvalue *asUpvalue(Value v) { return static_cast<ObjUpvalue *>(v.as.obj); }

}  // namespace luke
