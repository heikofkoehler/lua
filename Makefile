# Variables
LUA_TEST_URL = https://www.lua.org/tests/lua-5.5.0-tests.tar.gz
LUA_TEST_DIR = lua-5.5.0-tests

.PHONY: all build clean test clean-tests test-internal test-lua55

all: build

build:
	mkdir -p build && cd build && cmake .. && make

clean:
	rm -rf build

test: test-internal test-lua55

test-internal: build
	@echo "--- Running Internal Tests ---"
	cd tests && ./run_all_tests.sh ../build/lua

test-lua55: build
	@echo "--- Preparing Lua 5.5.0 Test Suite ---"
	@if [ ! -d "$(LUA_TEST_DIR)" ]; then \
		curl -s $(LUA_TEST_URL) | tar xz; \
	fi
	@cp ./build/lua ./$(LUA_TEST_DIR)/
	@echo "--- Running Lua 5.5.0 Tests ---"
	@cd $(LUA_TEST_DIR) && ./lua all.lua
	@echo "--- Tests Completed Successfully ---"

clean-tests:
	rm -rf $(LUA_TEST_DIR)
