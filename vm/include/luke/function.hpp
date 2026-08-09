#pragma once

#include "luke/bytecode.hpp"
#include "luke/value.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace luke {

struct ObjClass;

struct UpvalueDesc {
  bool isLocal = true;
  uint8_t index = 0;
};

struct ObjFunction : Obj {
  std::string name;
  int arity = 0;
  Chunk chunk;
  ObjClass *klass = nullptr;
  bool isMethod = false;
  bool isStatic = false;
  bool isPrivate = false;
  std::vector<UpvalueDesc> upvalueDescs;
};

inline bool isFunction(Value v) {
  return v.isObj() && v.as.obj && v.as.obj->type == ObjType::Function;
}

inline ObjFunction *asFunction(Value v) {
  return static_cast<ObjFunction *>(v.as.obj);
}

}  // namespace luke
