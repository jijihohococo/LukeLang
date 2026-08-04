CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS ?=

SRCS := \
	src/value.cpp \
	src/heap.cpp \
	src/bytecode.cpp \
	src/vm.cpp \
	src/compiler.cpp \
	src/main.cpp

OBJS := $(SRCS:src/%.cpp=build/%.o)

.PHONY: all clean test

all: build/luke

build/luke: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build:
	mkdir -p build

test: build/luke
	./build/luke SHOW ../examples/native/hello.luke
	./build/luke SHOW ../examples/native/counter.luke
	./build/luke SHOW ../examples/native/functions.luke
	./build/luke SHOW ../examples/native/oop.luke
	./build/luke SHOW ../examples/native/advanced_oop.luke
	./build/luke SHOW ../examples/native/closures.luke
	./build/luke SHOW ../examples/native/privacy.luke

clean:
	rm -rf build
