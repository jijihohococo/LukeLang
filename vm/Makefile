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
	./build/luke BUILD ../examples/build/modules.luke -o build/ex_modules
	./build/ex_modules
	./build/luke BUILD ../examples/build/json_demo.luke -o build/ex_json
	./build/ex_json
	./build/luke BUILD ../examples/build/http_demo.luke -o build/ex_http
	./build/ex_http
	@mkdir -p build/http_fixture && cp ../examples/build/luke.json build/http_fixture/luke.json
	@python3 -m http.server 8765 --directory build/http_fixture >/tmp/luke_http_server.log 2>&1 & echo $$! > /tmp/luke_http_server.pid; sleep 0.4
	./build/luke BUILD ../examples/build/http_json.luke -o build/ex_http_json
	./build/ex_http_json
	@kill `cat /tmp/luke_http_server.pid` 2>/dev/null || true
	@# negative typechecks must fail
	@./build/luke BUILD ../examples/build/bad_types.luke -o build/bad_types 2>/tmp/luke_bad_types.txt; test $$? -ne 0
	@grep -q "wants TEXT but got NUMBER" /tmp/luke_bad_types.txt
	@./build/luke BUILD ../examples/build/bad_arity.luke -o build/bad_arity 2>/tmp/luke_bad_arity.txt; test $$? -ne 0
	@grep -q "expects 2 arguments" /tmp/luke_bad_arity.txt
	./build/luke BUILD ../examples/build/hello_wasm.luke -target wasm -o build/ex_hello.wasm
	node ../scripts/run_wasi.cjs build/ex_hello.wasm

clean:
	rm -rf build
