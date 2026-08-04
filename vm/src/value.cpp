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
      if (as.obj->type == ObjType::String) {
        return static_cast<ObjString *>(as.obj)->chars;
      }
      if (as.obj->type == ObjType::Array) {
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
      return "<object>";
    }
  }
  return "<unknown>";
}

}  // namespace luke
