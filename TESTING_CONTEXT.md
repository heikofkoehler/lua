### Lua 5.5.0 Integration & Automation

**Objective:**
Automate the retrieval, execution, and maintenance of the Lua 5.5.0 test suite within the custom interpreter's build system. Don't enable JIT.

---

### 1. Makefile Integration

The `test` target in the primary Makefile is enhanced to handle the test suite lifecycle.

Following is an example that must be adapted for this Lua implmentation:

```makefile
# Variables
LUA_TEST_URL = https://www.lua.org/tests/lua-5.5.0-tests.tar.gz
LUA_TEST_DIR = lua-5.5.0-tests

.PHONY: test clean-tests

test: all
	@echo "--- Preparing Lua 5.5.0 Test Suite ---"
	@if [ ! -d "$(LUA_TEST_DIR)" ]; then \
		curl -s $(LUA_TEST_URL) | tar xz; \
	fi
	@cp ./lua ./$(LUA_TEST_DIR)/
	@cp ./luac ./$(LUA_TEST_DIR)/ 2>/dev/null || true
	@echo "--- Running Tests ---"
	@cd $(LUA_TEST_DIR) && ./lua all.lua
	@echo "--- Tests Completed Successfully ---"

clean-tests:
	rm -rf $(LUA_TEST_DIR)

```

### 2. Agent "Fix & Commit" Protocol

When the agent executes `make test`, it must follow these steps if a failure occurs:

#### **A. Failure Analysis**

1. **First run tests individuallly:** Don't rely on `all.lua` first but verify functionally but running individual tests first.
1. **Identify the File:** Locate the specific `.lua` test file that failed from the output (e.g., `gc.lua`, `strings.lua`).
1. **Trace the Bug:** Determine if the root cause is in the interpreter's C++ source or a mismatch in the test environment.
1. **Apply Fix:** Modify the relevant interpreter code or the test script to resolve the assertion.
1. **Lastly run all.lua:** After all individual tests passed do one final pass executing `all.lua`

#### **B. Commitment Standards**

Every fix must be committed with a detailed description. The agent should use the following format:

* **Subject:** `fix: resolve [Test Name] failure in 5.5.0 suite`
* **Body:** * **Test Broken:** Specific file and line number (e.g., `api.lua:210`).
* **Root Cause:** Technical explanation of why the test failed (e.g., "Incorrect handling of light userdata in `lua_pushcclosure`").
* **Fix:** Brief summary of the code change implemented.

---

### 3. Verification

* A test run is only considered "Passing" if the interpreter reaches the end of `all.lua` and prints the final success message (typically `final OK`).
* Non-zero exit codes must trigger the Fix & Commit protocol.
