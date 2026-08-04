#pragma once

#include "luke/function.hpp"
#include "luke/value.hpp"

#include <cstddef>
#include <functional>
#include <vector>

namespace luke {

class Heap {
 public:
  explicit Heap(std::size_t gcThresholdBytes = 256 * 1024);
  ~Heap();

  Heap(const Heap &) = delete;
  Heap &operator=(const Heap &) = delete;

  ObjString *allocateString(std::string chars);
  ObjArray *allocateArray(std::size_t capacity = 0);
  ObjFunction *allocateFunction(std::string name, int arity);

  // Mark-sweep GC. Roots are supplied by the caller (VM stack, globals, etc.).
  void collect(const std::vector<Value *> &roots);
  void collect(const std::function<void(std::function<void(Value &)>)> &traceRoots);

  std::size_t bytesAllocated() const { return bytesAllocated_; }
  std::size_t objectCount() const { return objectCount_; }
  std::size_t collections() const { return collections_; }

 private:
  void maybeCollect();
  void markValue(Value &v);
  void markObject(Obj *obj);
  void sweep();
  void freeObject(Obj *obj);
  void track(Obj *obj, std::size_t bytes);

  Obj *objects_ = nullptr;
  std::size_t bytesAllocated_ = 0;
  std::size_t nextGc_ = 0;
  std::size_t objectCount_ = 0;
  std::size_t collections_ = 0;
  bool collecting_ = false;
};

}  // namespace luke
