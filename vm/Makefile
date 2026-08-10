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
	./build/luke BUILD ../examples/build/argus_demo.luke -target browser -o build/argus_demo
	@node ../scripts/luke_browser_loader.cjs build/argus_demo.wasm 2>&1 | grep -q "argus_upsert"
	./build/luke BUILD ../examples/build/hanka_demo.luke -target browser -o build/hanka_demo
	@node ../scripts/luke_browser_loader.cjs build/hanka_demo.wasm 2>&1 | grep -q "argus_upsert"
	./build/luke BUILD ../examples/build/web_app.luke -target browser -o build/web_app
	@node ../scripts/luke_browser_loader.cjs build/web_app.wasm 2>&1 | grep -q "route_go"
	@grep -q 'event:"route"' build/web_app.html
	@grep -q 'event:"fetch"' build/web_app.html
	@grep -qE 'hanka_slot_input|argus_place_input' build/web_app.luke.c
	@grep -q '_luke_fe_i' build/web_app.luke.c
	@grep -q 'hanka_begin_row' build/web_app.luke.c
	./build/luke BUILD ../examples/build/hanka_align.luke -o build/ex_hanka_align
	./build/ex_hanka_align | grep -q "align=1"
	./build/ex_hanka_align | grep -q "measure="
	@grep -q 'hanka_set_align' build/ex_hanka_align.luke.c
	./build/luke BUILD ../examples/build/frontend_widgets.luke -o build/ex_frontend_widgets
	./build/ex_frontend_widgets | grep -q "ok=1"
	./build/ex_frontend_widgets | grep -q "vw="
	@grep -qE 'hanka_slot_select|hanka_slot_table|hanka_slot_modal' build/ex_frontend_widgets.luke.c
	./build/luke PUBLISH WEB ../examples/build/frontend_widgets.luke -o build/frontend_widgets_pub
	@test -f build/frontend_widgets_pub.html
	@test -f build/frontend_widgets_pub.wasm
	@grep -q 'argus_a11y\|measure_text\|argus_select' build/frontend_widgets_pub.html
	./build/luke BUILD ../examples/build/frontend_wrap_forms.luke -o build/ex_frontend_wrap_forms
	./build/ex_frontend_wrap_forms | grep -q "wrap=1"
	./build/ex_frontend_wrap_forms | grep -q "vh="
	@grep -q 'hanka_set_wrap' build/ex_frontend_wrap_forms.luke.c
	@grep -qE 'inputType|, 3\)|, 4\)' build/ex_frontend_wrap_forms.luke.c
	@grep -q 'argus_fade_to' build/ex_frontend_wrap_forms.luke.c
	./build/luke BUILD ../examples/build/frontend_wrap_forms.luke -target browser -o build/frontend_wrap_forms
	@grep -q 'event:"viewport"' build/frontend_wrap_forms.html
	@grep -q 'viewport_height\|argus_fade\|now_ms' build/frontend_wrap_forms.html
	@node ../scripts/luke_browser_loader.cjs build/frontend_wrap_forms.wasm 2>&1 | grep -q "argus_upsert\|ok=1\|wrap=1"
	./build/luke BUILD ../examples/build/frontend_stress.luke -o build/ex_frontend_stress
	./build/ex_frontend_stress | grep -q "nodes=100"
	./build/ex_frontend_stress | grep -q "ms="
	./build/ex_frontend_stress | grep -q "ok=1"
	./build/luke BUILD ../examples/build/reactive_core.luke -o build/ex_reactive
	./build/ex_reactive | grep -q "total=300"
	./build/ex_reactive | grep -q "total=400"
	./build/ex_reactive | grep -q "total=500"
	./build/luke BUILD ../examples/build/reactive_cycle.luke -o build/ex_reactive_cycle
	@./build/ex_reactive_cycle 2>/tmp/luke_rx_cycle.txt >/dev/null; grep -q "cycle detected" /tmp/luke_rx_cycle.txt
	./build/luke BUILD ../examples/build/reactive_greeting.luke -target browser -o build/reactive_greeting
	@grep -q 'luke_rx_ui_set_text\|luke_rx_ui_enable' build/reactive_greeting.luke.c
	@grep -q 'luke_rx_write_text\|luke_rx_cell_text' build/reactive_greeting.luke.c
	@! grep -q 'argus_clear' build/reactive_greeting.luke.c || ! grep -A20 'luke_when_' build/reactive_greeting.luke.c | grep -q 'argus_clear'
	@node ../scripts/luke_browser_loader.cjs build/reactive_greeting.wasm --dispatch go:click 2>&1 | tee /tmp/luke_rx_ui.txt | grep -q 'Welcome, Alex'
	@grep -q '\[argus_flex\]' /tmp/luke_rx_ui.txt
	@grep -q '\[argus_flow_frame\]' /tmp/luke_rx_ui.txt
	@grep -q '\[argus_parent\].*greeting' /tmp/luke_rx_ui.txt
	@grep -q 'region=1' /tmp/luke_rx_ui.txt
	@! grep -q '\[argus_clear\]' /tmp/luke_rx_ui.txt
	./build/luke BUILD ../examples/build/reactive_counter_scope.luke -o build/ex_rx_scope
	./build/ex_rx_scope | grep -q "count=3"
	./build/ex_rx_scope | grep -q "doubled=6"
	./build/ex_rx_scope | grep -q "disposed"
	./build/luke BUILD ../examples/build/reactive_counter.luke -target browser -o build/reactive_counter
	@grep -q 'luke_rx_scope_begin\|luke_rx_scope_end' build/reactive_counter.luke.c
	@node ../scripts/luke_browser_loader.cjs build/reactive_counter.wasm --dispatch counter-inc:click 2>&1 | tee /tmp/luke_rx_counter.txt | grep -q 'Count: 1'
	@node ../scripts/luke_browser_loader.cjs build/reactive_counter.wasm --dispatch counter-destroy:click 2>&1 | grep -q 'Counter scope destroyed'
	./build/luke BUILD ../examples/build/reactive_fetch.luke -target browser -o build/reactive_fetch
	@grep -q 'luke_rx_write_text.*fetch_body\|luke_js_fetch_body' build/reactive_fetch.luke.c
	@grep -q 'luke_rx_batch_begin' build/reactive_fetch.luke.c
	@node -e 'const {runLukeWasm}=require("../scripts/luke_browser_loader.cjs");const fs=require("fs");const html=fs.readFileSync("build/reactive_fetch.html","utf8");const m=html.match(/var LUKE_WHENS = (\[[\s\S]*?\]);/);const whens=Function("return ("+m[1]+");")();runLukeWasm(fs.readFileSync("build/reactive_fetch.wasm"),{whens}).then(()=>{globalThis.__lukeDispatch("load","click");return new Promise(r=>setTimeout(r,80));}).then(()=>process.exit(0));' 2>&1 | tee /tmp/luke_rx_fetch.txt | grep -q 'status: 200'
	@grep -q 'stub/profile' /tmp/luke_rx_fetch.txt
	./build/luke BUILD ../examples/build/reactive_list.luke -o build/ex_rx_list
	./build/ex_rx_list | grep -q "n=2"
	./build/ex_rx_list | grep -q "item=Zoe"
	./build/ex_rx_list | grep -q "delta=1"
	./build/ex_rx_list | grep -q "Ada=10"
	./build/ex_rx_list | grep -q "fe=Zoe"
	@grep -q 'luke_rx_list_set' build/ex_rx_list.luke.c
	@grep -q 'luke_rx_ui_paint_list' build/ex_rx_list.luke.c
	./build/luke BUILD ../examples/build/reactive_list_ui.luke -target browser -o build/reactive_list_ui
	@grep -q 'luke_rx_ui_paint_list\|luke_rx_list_set' build/reactive_list_ui.luke.c
	@node ../scripts/luke_browser_loader.cjs build/reactive_list_ui.wasm --dispatch rename:click 2>&1 | tee /tmp/luke_rx_list.txt | grep -q 'Zoe'
	@! grep -q '\[argus_clear\]' /tmp/luke_rx_list.txt
	./build/luke BUILD ../examples/build/reactive_timeline.luke -o build/ex_rx_timeline
	./build/ex_rx_timeline | grep -q "progress=1"
	@grep -q 'luke_rx_timeline_run_sync' build/ex_rx_timeline.luke.c
	./build/luke BUILD ../examples/build/reactive_timeline_ui.luke -target browser -o build/reactive_timeline_ui
	@grep -q 'luke_timeline_progress\|BIND OPACITY\|luke_rx_ui_set_opacity' build/reactive_timeline_ui.luke.c
	@node -e 'const {runLukeWasm}=require("../scripts/luke_browser_loader.cjs");const fs=require("fs");const html=fs.readFileSync("build/reactive_timeline_ui.html","utf8");const m=html.match(/var LUKE_WHENS = (\[[\s\S]*?\]);/);const whens=Function("return ("+m[1]+");")();runLukeWasm(fs.readFileSync("build/reactive_timeline_ui.wasm"),{whens}).then(()=>{globalThis.__lukeDispatch("play","click");return new Promise(r=>setTimeout(r,350));}).then(()=>process.exit(0));' 2>&1 | tee /tmp/luke_rx_timeline.txt | grep -q 'Timeline finished'
	./build/luke BUILD ../examples/build/reactive_entity.luke -o build/ex_rx_entity
	./build/ex_rx_entity | grep -q "hp=100"
	./build/ex_rx_entity | grep -q "hp=75"
	./build/ex_rx_entity | grep -q "pct=0.75"
	@grep -q 'Player_health' build/ex_rx_entity.luke.c
	./build/luke BUILD ../examples/build/reactive_query.luke -o build/ex_rx_query
	./build/ex_rx_query | grep -q "alpha"
	./build/ex_rx_query | grep -q "gamma"
	@grep -q 'luke_rx_query_refresh' build/ex_rx_query.luke.c
	./build/luke BUILD ../examples/build/reactive_conformance_batch.luke -o build/ex_rx_conf_batch
	./build/ex_rx_conf_batch | grep -q "flush1=1"
	./build/ex_rx_conf_batch | grep -q "flush2=2"
	./build/ex_rx_conf_batch | grep -q "flush3=3"
	./build/luke BUILD ../examples/build/reactive_conformance_stale.luke -o build/ex_rx_conf_stale
	./build/ex_rx_conf_stale | grep -q "stale=2"
	./build/ex_rx_conf_stale | grep -q "total_stale=4"
	./build/ex_rx_conf_stale | grep -q "sum_stale=2"
	./build/luke BUILD ../examples/build/reactive_conformance_order.luke -o build/ex_rx_conf_order
	./build/ex_rx_conf_order | grep -q "chain=4"
	./build/ex_rx_conf_order | grep -q "chain=14"
	./build/ex_rx_conf_order | grep -q "derived_runs=3"
	./build/luke BUILD ../examples/build/reactive_conformance_priority.luke -o build/ex_rx_conf_prio
	./build/ex_rx_conf_prio | grep -q "log=10"
	./build/ex_rx_conf_prio | grep -q "ui_first=1"
	./build/luke BUILD ../examples/build/reactive_conformance_nested.luke -o build/ex_rx_conf_nested
	./build/ex_rx_conf_nested | grep -q "y=1"
	./build/ex_rx_conf_nested | grep -q "passes=1"
	./build/ex_rx_conf_nested | grep -q "turns=1"
	./build/ex_rx_conf_nested | grep -q "deferred=1"
	./build/luke BUILD ../examples/build/reactive_conformance_dedup.luke -o build/ex_rx_conf_dedup
	./build/ex_rx_conf_dedup | grep -q "dedup=2"
	./build/ex_rx_conf_dedup | grep -q "t=5"
	./build/luke BUILD ../examples/build/reactive_conformance_subtree.luke -o build/ex_rx_conf_subtree
	./build/ex_rx_conf_subtree | grep -q "subtree=1"
	./build/ex_rx_conf_subtree | grep -q "after=100"
	./build/luke BUILD ../examples/build/reactive_conformance_memory.luke -o build/ex_rx_conf_mem
	./build/ex_rx_conf_mem | grep -q "alive=2"
	./build/ex_rx_conf_mem | grep -q "disposed=2"
	./build/ex_rx_conf_mem | grep -q "dead=2"
	./build/ex_rx_conf_mem | grep -q "leaks=0"
	./build/ex_rx_conf_mem | grep -q "count=0"
	./build/luke BUILD ../examples/build/reactive_conformance_weak.luke -o build/ex_rx_conf_weak
	./build/ex_rx_conf_weak | grep -q "snap0=10"
	./build/ex_rx_conf_weak | grep -q "snap=10"
	./build/ex_rx_conf_weak | grep -q "live=15"
	./build/ex_rx_conf_weak | grep -q "weak_reads=1"
	./build/luke BUILD ../examples/build/reactive_conformance_scope_gc.luke -o build/ex_rx_conf_scope_gc
	./build/ex_rx_conf_scope_gc | grep -q "scopes=0"
	./build/ex_rx_conf_scope_gc | grep -q "gc=2"
	./build/ex_rx_conf_scope_gc | grep -q "alive=0"
	./build/ex_rx_conf_scope_gc | grep -q "dead=2"
	./build/luke BUILD ../examples/build/reactive_conformance_weak_effect.luke -o build/ex_rx_conf_weak_fx
	./build/ex_rx_conf_weak_fx | grep -q "tick"
	./build/ex_rx_conf_weak_fx | grep -q "leaks=0"
	./build/ex_rx_conf_weak_fx | grep -q "alive=1"
	./build/ex_rx_conf_weak_fx | grep -q "weak_reads=1"
	./build/luke BUILD ../examples/build/reactive_conformance_devtools.luke -o build/ex_rx_conf_devtools
	./build/ex_rx_conf_devtools | grep -q "write=1"
	./build/ex_rx_conf_devtools | grep -q "root=1"
	./build/ex_rx_conf_devtools | grep -q "depth=1"
	./build/ex_rx_conf_devtools | grep -q "deps=2"
	./build/ex_rx_conf_devtools | grep -q "subs=1"
	./build/ex_rx_conf_devtools | grep -q "cells=2"
	./build/ex_rx_conf_devtools | grep -q "derived=1"
	./build/ex_rx_conf_devtools | grep -q "edges=2"
	./build/ex_rx_conf_devtools | grep -q "step0=3"
	./build/ex_rx_conf_devtools | grep -q "wave0=2"
	./build/ex_rx_conf_devtools | grep -q "traces=1"
	./build/ex_rx_conf_devtools | grep -q "dumps=1"
	./build/luke BUILD ../examples/build/reactive_conformance_error.luke -o build/ex_rx_conf_error
	./build/ex_rx_conf_error 2>/dev/null | grep -q "errors=1"
	./build/ex_rx_conf_error 2>/dev/null | grep -q "iso=1"
	./build/ex_rx_conf_error 2>/dev/null | grep -q "ok=1"
	./build/ex_rx_conf_error 2>/dev/null | grep -q "async=1"
	./build/ex_rx_conf_error 2>/dev/null | grep -q "errors2=2"
	./build/ex_rx_conf_error 2>/dev/null | grep -q "retries=1"
	./build/luke BUILD ../examples/build/reactive_conformance_boundary.luke -o build/ex_rx_conf_boundary
	./build/ex_rx_conf_boundary 2>/dev/null | grep -q "global=1"
	./build/ex_rx_conf_boundary 2>/dev/null | grep -q "trips=1"
	./build/ex_rx_conf_boundary 2>/dev/null | grep -q "tripped=1"
	./build/ex_rx_conf_boundary 2>/dev/null | grep -q "reset=0"
	./build/luke BUILD ../examples/build/fullstack_cell_bind_proof.luke -o build/ex_fullstack_bind
	./build/ex_fullstack_bind | tee /tmp/luke_fullstack_bind.txt | grep -q "bind_region=1"
	@grep -q "ch1_region=1" /tmp/luke_fullstack_bind.txt
	@grep -q "ch2_region=1" /tmp/luke_fullstack_bind.txt
	@grep -q "ch1_granular=2" /tmp/luke_fullstack_bind.txt
	@grep -q "ch2_granular=3" /tmp/luke_fullstack_bind.txt
	./build/luke BUILD ../examples/build/fullstack_cell_server.luke -o build/fullstack_cell_server
	./build/luke BUILD ../examples/build/fullstack_cell_client.luke -target browser -o build/fullstack_cell_client
	@grep -q 'luke_rx_ui_set_text\|luke_rx_batch_begin' build/fullstack_cell_client.luke.c
	@rm -f /tmp/luke_fullstack_server.pid /tmp/luke_fullstack_cell.txt
	@./build/fullstack_cell_server > /tmp/luke_fullstack_server.log 2>&1 & echo $$! > /tmp/luke_fullstack_server.pid
	@sleep 0.15
	@node ../scripts/luke_browser_loader.cjs build/fullstack_cell_client.wasm --dispatch start:click --wait-ms 700 2>&1 | tee /tmp/luke_fullstack_cell.txt | grep -q "region=1"
	@grep -q "granular_delta=1" /tmp/luke_fullstack_cell.txt
	@grep -q "cell=" /tmp/luke_fullstack_cell.txt
	@if [ -f /tmp/luke_fullstack_server.pid ]; then kill $$(cat /tmp/luke_fullstack_server.pid) 2>/dev/null || true; fi
	./build/luke BUILD ../examples/build/reactive_benchmark.luke -o build/reactive_benchmark
	./build/reactive_benchmark | tee /tmp/luke_bench.txt | grep -q "benchmark_ok=1"
	@grep -q "nodes=1000" /tmp/luke_bench.txt
	@grep -q "nodes=10000" /tmp/luke_bench.txt
	@test $$(grep -c 'region=1' /tmp/luke_bench.txt) -ge 2
	./build/luke BUILD ../examples/build/dashboard_server.luke -o build/dashboard_server
	./build/luke BUILD ../examples/build/dashboard_client.luke -target browser -o build/dashboard_client
	@grep -q 'luke_rx_ui_set_text\|luke_rx_batch_begin' build/dashboard_client.luke.c
	@grep -q 'hanka_begin_column\|hanka_begin_row' build/dashboard_client.luke.c
	@rm -f /tmp/luke_dash_server.pid /tmp/luke_dash.txt
	@./build/dashboard_server > /tmp/luke_dash_server.log 2>&1 & echo $$! > /tmp/luke_dash_server.pid
	@sleep 0.15
	@node ../scripts/luke_browser_loader.cjs build/dashboard_client.wasm --dispatch go:click --wait-ms 600 2>&1 | tee /tmp/luke_dash.txt | grep -q "region=1"
	@grep -q "granular_delta=1" /tmp/luke_dash.txt
	@grep -q "\[argus_flex\]" /tmp/luke_dash.txt
	@if [ -f /tmp/luke_dash_server.pid ]; then kill $$(cat /tmp/luke_dash_server.pid) 2>/dev/null || true; fi
	@rm -rf ../luke_modules/_test_pkg && ./build/luke PKG init _test_pkg && test -f ../luke_modules/_test_pkg/main.luke && rm -rf ../luke_modules/_test_pkg

clean:
	rm -rf build
