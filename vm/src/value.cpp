#include "luke/function.hpp"
#include "luke/object.hpp"
#include "luke/value.hpp"

#include <sstream>

namespace luke {

bool Value::isTruthy() const {
  switch (type) {
    case ValueType::Nil:
      return false;
    case ValueType::Bool:
      return as.boolean;
    case ValueType::Number:
      return as.number != 0.0;
    case ValueType::Obj:
      return as.obj != nullptr;
  }
  return false;
}

std::string Value::toString() const {
  switch (type) {
    case ValueType::Nil:
      return "nil";
    case ValueType::Bool:
      return as.boolean ? "true" : "false";
    case ValueType::Number: {
      std::ostringstream oss;
      oss << as.number;
      return oss.str();
    }
    case ValueType::Obj: {
      if (!as.obj) return "nil";
      switch (as.obj->type) {
        case ObjType::String:
          return static_cast<ObjString *>(as.obj)->chars;
        case ObjType::Array: {
          auto *arr = static_cast<ObjArray *>(as.obj);
          std::ostringstream oss;
          oss << "[";
          for (std::size_t i = 0; i < arr->items.size(); ++i) {
            if (i) oss << ", ";
            oss << arr->items[i].toString();
          }
          oss << "]";
          return oss.str();
        }
        case ObjType::Function: {
          auto *fn = static_cast<ObjFunction *>(as.obj);
          return "<fn " + (fn->name.empty() ? "?" : fn->name) + ">";
        }
        case ObjType::Class: {
          auto *klass = static_cast<ObjClass *>(as.obj);
          return "<blueprint " + klass->name + ">";
        }
        case ObjType::Instance: {
          auto *inst = static_cast<ObjInstance *>(as.obj);
          return "<" + (inst->klass ? inst->klass->name : "?") + " instance>";
        }
        case ObjType::BoundMethod:
          return "<bound method>";
      }
      return "<object>";
    }
  }
  return "<unknown>";
}

}  // namespace luke
