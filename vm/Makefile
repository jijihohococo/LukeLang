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
	./build/luke SHOW ../examples/native/hello.luke --vm
	./build/luke SHOW ../examples/native/counter.luke --vm
	./build/luke SHOW ../examples/native/functions.luke --vm
	./build/luke SHOW ../examples/native/oop.luke --vm
	./build/luke SHOW ../examples/native/advanced_oop.luke --vm
	./build/luke SHOW ../examples/native/closures.luke --vm
	./build/luke SHOW ../examples/native/privacy.luke --vm
	@# Build-first SHOW (no --vm) for a Build program
	./build/luke SHOW ../examples/build/hello.luke
	@# Play-only feature falls back to VM
	./build/luke SHOW ../examples/native/closures.luke

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
	./build/luke BUILD ../examples/build/packages.luke -o build/ex_packages
	./build/ex_packages
	./build/luke BUILD ../examples/build/tooling.luke -o build/ex_tooling
	./build/ex_tooling
	./build/luke BUILD ../examples/build/arena_scope.luke -o build/ex_arena
	./build/ex_arena
	./build/luke BUILD ../examples/build/ffi_math.luke -o build/ex_ffi
	./build/ex_ffi
	@rm -rf ../luke_modules/echo
	./build/luke PKG install echo
	./build/luke BUILD ../examples/build/registry_echo.luke -o build/ex_reg
	./build/ex_reg
	./build/luke IR ../examples/build/functions.luke >/tmp/luke_ir.txt
	@grep -q "luke-build-ir" /tmp/luke_ir.txt
	./build/luke SHOW ../examples/native/play_import.luke --vm
	./build/luke BUILD ../examples/build/json_demo.luke -o build/ex_json
	./build/ex_json
	./build/luke BUILD ../examples/build/http_demo.luke -o build/ex_http
	./build/ex_http
	@mkdir -p build/http_fixture && cp ../examples/build/luke.json build/http_fixture/luke.json
	@python3 -m http.server 8765 --directory build/http_fixture >/tmp/luke_http_server.log 2>&1 & echo $$! > /tmp/luke_http_server.pid; sleep 0.4
	./build/luke BUILD ../examples/build/http_json.luke -o build/ex_http_json
	./build/ex_http_json
	@kill `cat /tmp/luke_http_server.pid` 2>/dev/null || true
	./build/luke BUILD ../examples/build/collections.luke -o build/ex_collections
	./build/ex_collections | grep -q "caught:"
	./build/luke TEST ../examples/build/collections_test.luke
	./build/luke BUILD ../examples/build/sqlite_demo.luke -o build/ex_sqlite
	./build/ex_sqlite | grep -q "hello from LukeLang"
	./build/luke PKG lock
	@test -f ../luke.lock
	@# negative typechecks must fail
	@./build/luke BUILD ../examples/build/bad_types.luke -o build/bad_types 2>/tmp/luke_bad_types.txt; test $$? -ne 0
	@grep -q "wants TEXT but got NUMBER" /tmp/luke_bad_types.txt
	@./build/luke BUILD ../examples/build/bad_arity.luke -o build/bad_arity 2>/tmp/luke_bad_arity.txt; test $$? -ne 0
	@grep -q "expects 2 arguments" /tmp/luke_bad_arity.txt
	./build/luke BUILD ../examples/build/hello_wasm.luke -target wasm -o build/ex_hello.wasm
	node ../scripts/run_wasi.cjs build/ex_hello.wasm
	./build/luke BUILD ../examples/build/hello_browser.luke -target browser -o build/ex_browser
	node ../scripts/luke_browser_loader.cjs build/ex_browser.wasm
	@test -f build/ex_browser.html
	@node ../scripts/luke_browser_loader.cjs build/ex_browser.wasm | grep -q "Hello from Luke in the browser"
	./build/luke BUILD ../examples/build/hello_js.luke -target browser -o build/ex_js
	@node ../scripts/luke_browser_loader.cjs build/ex_js.wasm | grep -q "jsSetText"
	@mkdir -p ../sample/dist
	./build/luke BUILD ../sample/landing.luke -target browser -o ../sample/dist/landing
	@grep -q '@font-face' ../sample/dist/landing.html
	@grep -q 'luke_when_' ../sample/dist/landing.html
	@test -f ../sample/dist/fonts/syne-700.woff2
	@test ! -f ../sample/dist/luke_browser_loader.js
	@node ../scripts/luke_browser_loader.cjs ../sample/dist/landing.wasm >/tmp/luke_sample_boot.txt
	./build/luke BUILD ../examples/build/render_demo.luke -target browser -o build/render_demo
	@node ../scripts/luke_browser_loader.cjs build/render_demo.wasm 2>&1 | grep -q "render_upsert"
	@rm -rf ../luke_modules/_test_pkg && ./build/luke PKG init _test_pkg && test -f ../luke_modules/_test_pkg/main.luke && rm -rf ../luke_modules/_test_pkg

clean:
	rm -rf build
