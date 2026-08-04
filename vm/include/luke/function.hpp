#pragma once

#include "luke/bytecode.hpp"
#include "luke/value.hpp"

#include <string>

namespace luke {

struct ObjClass;

struct ObjFunction : Obj {
  std::string name;
  int arity = 0;
  Chunk chunk;
  // Owning class when this function is a method (for CALL PARENT).
  ObjClass *klass = nullptr;
  bool isMethod = false;
};

inline bool isFunction(Value v) {
  return v.isObj() && v.as.obj && v.as.obj->type == ObjType::Function;
}

inline ObjFunction *asFunction(Value v) {
  return static_cast<ObjFunction *>(v.as.obj);
}

}  // namespace luke
