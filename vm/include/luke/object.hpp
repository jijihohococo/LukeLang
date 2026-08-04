#pragma once

#include "luke/function.hpp"
#include "luke/value.hpp"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace luke {

struct ObjClass;
struct ObjInstance;
struct ObjBoundMethod;

struct ObjClass : Obj {
  std::string name;
  ObjClass *superclass = nullptr;
  std::unordered_map<std::string, ObjFunction *> methods;
  // Field defaults applied at construction (parent → child order via walk).
  std::vector<std::pair<std::string, Value>> fields;

  ObjFunction *findMethod(const std::string &name) const {
    for (const ObjClass *c = this; c; c = c->superclass) {
      auto it = c->methods.find(name);
      if (it != c->methods.end()) return it->second;
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

inline bool isClass(Value v) {
  return v.isObj() && v.as.obj && v.as.obj->type == ObjType::Class;
}
inline bool isInstance(Value v) {
  return v.isObj() && v.as.obj && v.as.obj->type == ObjType::Instance;
}
inline bool isBoundMethod(Value v) {
  return v.isObj() && v.as.obj && v.as.obj->type == ObjType::BoundMethod;
}

inline ObjClass *asClass(Value v) { return static_cast<ObjClass *>(v.as.obj); }
inline ObjInstance *asInstance(Value v) { return static_cast<ObjInstance *>(v.as.obj); }
inline ObjBoundMethod *asBoundMethod(Value v) {
  return static_cast<ObjBoundMethod *>(v.as.obj);
}

}  // namespace luke
