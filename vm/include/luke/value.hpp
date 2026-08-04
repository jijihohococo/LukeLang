#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace luke {

enum class ValueType : uint8_t {
  Nil = 0,
  Bool,
  Number,
  Obj,
};

enum class ObjType : uint8_t {
  String = 1,
  Array,
  Function,
  Class,
  Instance,
  BoundMethod,
  Contract,
  Closure,
  Upvalue,
};

struct Obj;

// Tagged runtime value. Objects live on the Luke heap; numbers/bools are unboxed.
struct Value {
  ValueType type = ValueType::Nil;
  union {
    bool boolean;
    double number;
    Obj *obj;
  } as{};

  static Value nil() {
    Value v;
    v.type = ValueType::Nil;
    return v;
  }
  static Value boolean(bool b) {
    Value v;
    v.type = ValueType::Bool;
    v.as.boolean = b;
    return v;
  }
  static Value number(double n) {
    Value v;
    v.type = ValueType::Number;
    v.as.number = n;
    return v;
  }
  static Value object(Obj *o) {
    Value v;
    v.type = ValueType::Obj;
    v.as.obj = o;
    return v;
  }

  bool isNil() const { return type == ValueType::Nil; }
  bool isBool() const { return type == ValueType::Bool; }
  bool isNumber() const { return type == ValueType::Number; }
  bool isObj() const { return type == ValueType::Obj; }

  bool isTruthy() const;
  std::string toString() const;
};

struct Obj {
  ObjType type;
  bool marked = false;
  Obj *next = nullptr;
};

struct ObjString : Obj {
  std::string chars;
};

struct ObjArray : Obj {
  std::vector<Value> items;
};

inline bool isString(Value v) {
  return v.isObj() && v.as.obj && v.as.obj->type == ObjType::String;
}

inline ObjString *asString(Value v) {
  return static_cast<ObjString *>(v.as.obj);
}

}  // namespace luke
