BUILD_DIR=build
TOOLS_BUILD_DIR=build-tools
BUILDTYPE?=Release
TJS_SANITIZE?=
TJS_DISABLE_SQLITE3?=OFF
TJS_DISABLE_WASM?=ON
TJS_BUILD_WASM_NATIVE_MODULE?=ON
TJS_WAMR_SIMD?=OFF
TJS_ENABLE_LVGL?=OFF
TJS_LVGL_USE_SDL?=OFF
TARGET?=
SANITIZE_BUILD_DIR?=build-asan
SANITIZE_BUILDTYPE?=RelWithDebInfo
SANITIZE?=address
SANITIZE_TESTS?=tests/test-mqtt-alias.js tests/test-mqtt-wrapper.js tests/test-mqtt-native-tls-options.js tests/test-mqtt-integration.js tests/test-mqtt-integration-batch.js tests/test-mqtt-integration-publish-large.js
ASAN_OPTIONS?=detect_leaks=0:abort_on_error=1:halt_on_error=1:detect_stack_use_after_return=1
UBSAN_OPTIONS?=halt_on_error=1:abort_on_error=1:print_stacktrace=1

JOBS?=$(shell getconf _NPROCESSORS_ONLN)
ifeq ($(JOBS),)
JOBS := $(shell sysctl -n hw.ncpu)
endif
ifeq ($(JOBS),)
JOBS := $(shell nproc)
endif
ifeq ($(JOBS),)
JOBS := 4
endif

XUAN=$(BUILD_DIR)/xuan
XUANC=$(TOOLS_BUILD_DIR)/xuanc
DIST_STAMP=$(BUILD_DIR)/dist/.stamp
STDLIB_MODULES=$(wildcard src/js/stdlib/*.js)
NPM_PATCHES=$(wildcard patches/*+*.patch)
ESBUILD?=npx esbuild
ESBUILD_PARAMS_COMMON=--target=esnext --platform=neutral --format=esm --main-fields=main,module
ESBUILD_PARAMS_MINIFY=--minify --keep-names
ESBUILD_PARAMS_POLYFILLS_MINIFY?=--minify
TJSC_PARAMS_STIP=-s
JS_NO_STRIP?=0

ifeq ($(JS_NO_STRIP),1)
	ESBUILD_PARAMS_MINIFY=
	ESBUILD_PARAMS_POLYFILLS_MINIFY=
	TJSC_PARAMS_STIP=
endif

all: $(if $(TARGET),build-target,$(DIST_STAMP))

build-target: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR) --target $(TARGET) -j $(JOBS)

.PHONY: apply-deps-patches apply-node-patches

# Local dependency changes are stored as patches and applied to the pinned
# submodule checkouts, so a fresh clone builds identically without forking.
# Format: <submodule-dir>:<patch-basename>
DEPS_PATCHES = libwebsockets:lws quickjs:quickjs libuv:libuv mimalloc:mimalloc

apply-deps-patches:
	@set -e; \
	for pair in $(DEPS_PATCHES); do \
		sub=$${pair%%:*}; \
		name=$${pair##*:}; \
		patch=$(CURDIR)/patches/$$name.patch; \
		if [ ! -f "$$patch" ]; then \
			echo "Missing patch file $$patch" >&2; exit 1; \
		fi; \
		if git -C deps/$$sub apply --reverse --check "$$patch" >/dev/null 2>&1; then \
			echo "Already applied $$name.patch"; \
		elif git -C deps/$$sub apply --check "$$patch" >/dev/null 2>&1; then \
			git -C deps/$$sub apply "$$patch"; \
			echo "Applied $$name.patch"; \
		else \
			echo "Failed to apply $$name.patch" >&2; exit 1; \
		fi; \
	done

apply-node-patches:
	@if [ -n "$(NPM_PATCHES)" ]; then \
		for patch in $(NPM_PATCHES); do \
			if git apply --reverse --check "$$patch" >/dev/null 2>&1; then \
				echo "Already applied $$(basename "$$patch")"; \
			elif git apply --check "$$patch" >/dev/null 2>&1; then \
				git apply "$$patch"; \
				echo "Applied $$(basename "$$patch")"; \
			else \
				echo "Failed to apply $$(basename "$$patch"); run npm install first or refresh the patch" >&2; \
				exit 1; \
			fi; \
		done; \
	fi

$(BUILD_DIR)/Makefile: | apply-deps-patches
	cmake -B $(BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=$(BUILDTYPE) \
		$(if $(TJS_SANITIZE),-DTJS_SANITIZE=$(TJS_SANITIZE),) \
		$(if $(filter-out OFF,$(TJS_DISABLE_SQLITE3)),-DTJS_DISABLE_SQLITE3=$(TJS_DISABLE_SQLITE3),) \
		$(if $(filter-out OFF,$(TJS_DISABLE_WASM)),-DTJS_DISABLE_WASM=$(TJS_DISABLE_WASM),) \
		$(if $(filter-out ON,$(TJS_BUILD_WASM_NATIVE_MODULE)),-DTJS_BUILD_WASM_NATIVE_MODULE=$(TJS_BUILD_WASM_NATIVE_MODULE),) \
		$(if $(filter-out AUTO,$(TJS_WAMR_SIMD)),-DTJS_WAMR_SIMD=$(TJS_WAMR_SIMD),) \
		$(if $(XUAN_ENABLE_HTTP2),-DXUAN_ENABLE_HTTP2=$(XUAN_ENABLE_HTTP2),) \
		$(if $(filter-out OFF,$(TJS_ENABLE_LVGL)),-DTJS_ENABLE_LVGL=$(TJS_ENABLE_LVGL),) \
		$(if $(filter-out OFF,$(TJS_LVGL_USE_SDL)),-DTJS_LVGL_USE_SDL=$(TJS_LVGL_USE_SDL),)

$(XUAN): apply-deps-patches js $(BUILD_DIR)/Makefile
	cmake --build $(BUILD_DIR) -j $(JOBS)

$(DIST_STAMP): $(XUAN)
	./scripts/assemble-dist.sh $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@touch $@

$(TOOLS_BUILD_DIR)/Makefile:
	cmake -B $(TOOLS_BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILDTYPE) -DTJS_BUILD_TOOLS_ONLY=ON

$(XUANC): $(TOOLS_BUILD_DIR)/Makefile
	cmake --build $(TOOLS_BUILD_DIR) --target xuanc -j $(JOBS)

src/bundles/js/core/polyfills-compat.js: src/js/polyfills/compat.js src/js/polyfills/*.js
	$(ESBUILD) src/js/polyfills/compat.js \
		--bundle \
		--metafile=$@.json \
		--outfile=$@ \
		$(ESBUILD_PARAMS_POLYFILLS_MINIFY) \
		$(ESBUILD_PARAMS_COMMON)

src/bundles/c/core/polyfills-compat.c: $(XUANC) src/bundles/js/core/polyfills-compat.js
	@mkdir -p $(basename $(dir $@))
	$(XUANC) -m \
		$(TJSC_PARAMS_STIP) \
		-o $@ \
		-n "polyfills-compat.js" \
		-p tjs__ \
		src/bundles/js/core/polyfills-compat.js

src/bundles/js/core/polyfills-lite.js: src/js/polyfills/lite.js src/js/polyfills/*.js
	$(ESBUILD) src/js/polyfills/lite.js \
		--bundle \
		--metafile=$@.json \
		--outfile=$@ \
		$(ESBUILD_PARAMS_POLYFILLS_MINIFY) \
		$(ESBUILD_PARAMS_COMMON)

src/bundles/c/core/polyfills-lite.c: $(XUANC) src/bundles/js/core/polyfills-lite.js
	@mkdir -p $(basename $(dir $@))
	$(XUANC) -m \
		$(TJSC_PARAMS_STIP) \
		-o $@ \
		-n "polyfills-lite.js" \
		-p tjs__ \
		src/bundles/js/core/polyfills-lite.js

src/bundles/js/core/polyfills-heavy.js: apply-node-patches src/js/polyfills/heavy.js src/js/polyfills/ws-stream.js $(NPM_PATCHES)
	$(ESBUILD) src/js/polyfills/heavy.js \
		--bundle \
		--metafile=$@.json \
		--outfile=$@ \
		$(ESBUILD_PARAMS_POLYFILLS_MINIFY) \
		$(ESBUILD_PARAMS_COMMON)

src/bundles/c/core/polyfills-heavy.c: $(XUANC) src/bundles/js/core/polyfills-heavy.js
	@mkdir -p $(basename $(dir $@))
	$(XUANC) -m \
		$(TJSC_PARAMS_STIP) \
		-o $@ \
		-n "polyfills-heavy.js" \
		-p tjs__ \
		src/bundles/js/core/polyfills-heavy.js

src/bundles/js/core/core.js: src/js/core/*.js
	$(ESBUILD) src/js/core/index.js \
		--bundle \
		--metafile=$@.json \
		--outfile=$@ \
		$(ESBUILD_PARAMS_MINIFY) \
		$(ESBUILD_PARAMS_COMMON)

src/bundles/c/core/core.c: $(XUANC) src/bundles/js/core/core.js
	@mkdir -p $(basename $(dir $@))
	$(XUANC) -m \
		$(TJSC_PARAMS_STIP) \
		-o $@ \
		-n "core.js" \
		-p tjs__ \
		src/bundles/js/core/core.js

src/bundles/js/core/run-main.js: src/js/run-main/*.js
	$(ESBUILD) src/js/run-main/index.js \
		--bundle \
		--metafile=$@.json \
		--outfile=$@ \
		--external:tjs:* \
		--external:node:* \
		--external:buffer \
		--external:wasm \
		--external:@jsr/* \
		$(ESBUILD_PARAMS_MINIFY) \
		$(ESBUILD_PARAMS_COMMON)

src/bundles/c/core/run-main.c: $(XUANC) src/bundles/js/core/run-main.js
	@mkdir -p $(basename $(dir $@))
	$(XUANC) -m \
		$(TJSC_PARAMS_STIP) \
		-o $@ \
		-n "run-main.js" \
		-p tjs__ \
		src/bundles/js/core/run-main.js

src/bundles/js/core/run-repl.js: src/js/run-repl/*.js
	$(ESBUILD) src/js/run-repl/index.js \
		--bundle \
		--metafile=$@.json \
		--outfile=$@ \
		--external:tjs:* \
		--external:node:* \
		--external:buffer \
		--log-override:direct-eval=silent \
		$(ESBUILD_PARAMS_MINIFY) \
		$(ESBUILD_PARAMS_COMMON)

src/bundles/c/core/run-repl.c: $(XUANC) src/bundles/js/core/run-repl.js
	@mkdir -p $(basename $(dir $@))
	$(XUANC) -m \
		$(TJSC_PARAMS_STIP) \
		-o $@ \
		-n "run-repl.js" \
		-p tjs__ \
		src/bundles/js/core/run-repl.js

src/bundles/c/core/worker-bootstrap.c: $(XUANC) src/js/worker/worker-bootstrap.js
	@mkdir -p $(basename $(dir $@))
	$(XUANC) \
		$(TJSC_PARAMS_STIP) \
		-o $@ \
		-n "worker-bootstrap.js" \
		-p tjs__ \
		src/js/worker/worker-bootstrap.js

core: src/bundles/c/core/polyfills-compat.c src/bundles/c/core/polyfills-lite.c src/bundles/c/core/polyfills-heavy.c src/bundles/c/core/core.c src/bundles/c/core/run-main.c src/bundles/c/core/run-repl.c src/bundles/c/core/worker-bootstrap.c

src/bundles/js/stdlib/%.js: src/js/stdlib/*.js src/js/stdlib/ffi/*.js src/mod_buffer.js tools/npm/src/*.js
	$(ESBUILD) src/js/stdlib/$(notdir $@) \
		--bundle \
		--outfile=$@ \
		--external:tjs:* \
		--external:node:* \
		--external:buffer \
		--external:crypto \
		$(ESBUILD_PARAMS_MINIFY) \
		$(ESBUILD_PARAMS_COMMON)

src/bundles/c/stdlib/%.c: $(XUANC) src/bundles/js/stdlib/%.js
	@mkdir -p $(basename $(dir $@))
	$(XUANC) -m \
		$(TJSC_PARAMS_STIP) \
		-o $@ \
		-n "tjs:$(basename $(notdir $@))" \
		-p tjs__ \
		src/bundles/js/stdlib/$(basename $(notdir $@)).js

stdlib: $(addprefix src/bundles/c/stdlib/, $(patsubst %.js, %.c, $(notdir $(STDLIB_MODULES))))

js: apply-node-patches $(XUANC) core stdlib

install: $(XUAN)
	cmake --build $(BUILD_DIR) --target install
	./scripts/assemble-dist.sh $(BUILD_DIR)

clean: $(BUILD_DIR)
	cmake --build $(BUILD_DIR) --target clean

debug:
	BUILDTYPE=Debug $(MAKE)

distclean:
	@rm -rf $(BUILD_DIR)
	@rm -rf src/bundles/js/

format:
	clang-format -i src/*.{c,h}

lint:
	npm run lint

test:
	./scripts/assemble-dist.sh $(BUILD_DIR)
	NODE_PATH=$(abspath $(BUILD_DIR))/dist/node_modules ./$(BUILD_DIR)/xuan test tests/

.PHONY: test_mem_santify test_mem_sanitize
test_mem_santify: test_mem_sanitize

test_mem_sanitize: apply-deps-patches
	cmake -B $(SANITIZE_BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=$(SANITIZE_BUILDTYPE) \
		-DTJS_SANITIZE=$(SANITIZE) \
		-DDISABLE_WERROR=ON \
		$(if $(filter-out OFF,$(TJS_DISABLE_SQLITE3)),-DTJS_DISABLE_SQLITE3=$(TJS_DISABLE_SQLITE3),) \
		$(if $(filter-out OFF,$(TJS_DISABLE_WASM)),-DTJS_DISABLE_WASM=$(TJS_DISABLE_WASM),) \
		$(if $(filter-out ON,$(TJS_BUILD_WASM_NATIVE_MODULE)),-DTJS_BUILD_WASM_NATIVE_MODULE=$(TJS_BUILD_WASM_NATIVE_MODULE),) \
		$(if $(filter-out AUTO,$(TJS_WAMR_SIMD)),-DTJS_WAMR_SIMD=$(TJS_WAMR_SIMD),) \
		$(if $(XUAN_ENABLE_HTTP2),-DXUAN_ENABLE_HTTP2=$(XUAN_ENABLE_HTTP2),) \
		$(if $(filter-out OFF,$(TJS_ENABLE_LVGL)),-DTJS_ENABLE_LVGL=$(TJS_ENABLE_LVGL),) \
		$(if $(filter-out OFF,$(TJS_LVGL_USE_SDL)),-DTJS_LVGL_USE_SDL=$(TJS_LVGL_USE_SDL),)
	$(MAKE) BUILD_DIR=$(SANITIZE_BUILD_DIR) BUILDTYPE=$(SANITIZE_BUILDTYPE) TJS_SANITIZE=$(SANITIZE) JS_NO_STRIP=1
	./scripts/assemble-dist.sh $(SANITIZE_BUILD_DIR)
	@for test in $(SANITIZE_TESTS); do \
		echo "SANITIZE $$test"; \
		ASAN_OPTIONS="$(ASAN_OPTIONS)" \
		UBSAN_OPTIONS="$(UBSAN_OPTIONS)" \
		NODE_PATH="$(abspath $(SANITIZE_BUILD_DIR))/dist/node_modules" \
		./$(SANITIZE_BUILD_DIR)/xuan run $$test || exit $$?; \
	done

test-advanced:
	cd tests/advanced && npm install
	./$(BUILD_DIR)/xuan --stack-size 10485760 test tests/advanced/

LVGL_TEST_DIR?=test-lvgl

test-lvgl test-lvgljs test_lvgl:
	./scripts/assemble-dist.sh $(BUILD_DIR)
	SDL_VIDEODRIVER=dummy TJS_TEST_CONCURRENCY=1 ./$(BUILD_DIR)/xuan test $(LVGL_TEST_DIR)

.PRECIOUS: src/bundles/js/core/%.js src/bundles/js/stdlib/%.js
.PHONY: all build-target js debug install clean distclean format lint test test-advanced test-lvgl test-lvgljs test_lvgl core stdlib $(XUAN) $(XUANC)
