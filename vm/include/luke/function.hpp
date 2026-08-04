#pragma once

#include "luke/bytecode.hpp"
#include "luke/value.hpp"

#include <string>

namespace luke {

struct ObjFunction : Obj {
  std::string name;
  int arity = 0;
  Chunk chunk;
};

inline bool isFunction(Value v) {
  return v.isObj() && v.as.obj && v.as.obj->type == ObjType::Function;
}

inline ObjFunction *asFunction(Value v) {
  return static_cast<ObjFunction *>(v.as.obj);
}

}  // namespace luke
