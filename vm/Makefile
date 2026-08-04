CXX ?= g++
CC ?= cc
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS ?=

SRCS := \
	src/value.cpp \
	src/heap.cpp \
	src/bytecode.cpp \
	src/vm.cpp \
	src/compiler.cpp \
	src/build_c.cpp \
	src/main.cpp

OBJS := $(SRCS:src/%.cpp=build/%.o)

.PHONY: all clean test test-play test-build

all: build/luke

build/luke: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build:
	mkdir -p build

test: test-play test-build

test-play: build/luke
	./build/luke SHOW ../examples/native/hello.luke
	./build/luke SHOW ../examples/native/counter.luke
	./build/luke SHOW ../examples/native/functions.luke
	./build/luke SHOW ../examples/native/oop.luke
	./build/luke SHOW ../examples/native/advanced_oop.luke
	./build/luke SHOW ../examples/native/closures.luke
	./build/luke SHOW ../examples/native/privacy.luke

test-build: build/luke
	./build/luke BUILD ../examples/build/hello.luke -o build/ex_hello
	./build/ex_hello
	./build/luke BUILD ../examples/build/counter.luke -o build/ex_counter
	./build/ex_counter
	./build/luke BUILD ../examples/build/functions.luke -o build/ex_functions
	./build/ex_functions
	./build/luke BUILD ../examples/build/oop.luke -o build/ex_oop
	./build/ex_oop

clean:
	rm -rf build
