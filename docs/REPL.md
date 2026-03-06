# REPL Features and Implementation

The Lua VM includes a modern, interactive REPL (Read-Eval-Print Loop) built on the `linenoise` library. It provides several features common in production-grade Lua implementations.

## Features

### 1. Auto-completion
The REPL supports intelligent tab-completion:
- **Global Variables**: Press `<TAB>` to see available globals.
- **Keywords**: Completes Lua keywords like `function`, `local`, `return`.
- **Table Members**: Dynamically traverses tables to complete members (e.g., `math.s<TAB>` suggests `math.sin`, `math.sqrt`).

### 2. Persistent History
Command history is automatically saved to `lua_history.txt` in the current working directory and reloaded when the REPL starts. Use the Up and Down arrow keys to navigate.

### 3. Multi-line Input
The REPL automatically detects incomplete statements by checking for specific parse errors (like `unexpected <eof>` or `unfinished string`). If a statement is incomplete, it provides a continuation prompt (`>> `).

### 4. Implicit Expression Evaluation
Any line that can be parsed as an expression is automatically evaluated and its results are printed.
- Input: `2 + 2`
- Output: `4` (in green)

### 5. Meta-commands
- `help`: Shows a summary of REPL features.
- `globals`: Lists all variables currently in the global namespace.
- `exit` or `quit`: Exits the REPL.
- `=expr`: Explicit shorthand for evaluating and printing an expression (e.g., `=_VERSION`).

## Implementation Details

The REPL logic is primarily located in `src/main.cpp` within the `repl()` function.

### Completion Callback
The `completion()` function in `main.cpp` is registered with `linenoise`. It performs the following:
1. Identifies the "word" currently being typed by looking back for separators.
2. If the word contains a dot (`.`), it splits the string and traverses the Lua global table to find the target table.
3. Iterates over the keys of the target table (or globals) and adds matching strings to the completion list.

### Evaluation Strategy
The REPL uses a "try-return" strategy:
1. It first attempts to compile the input string prefixed with `return `.
2. If compilation succeeds and execution is successful, it prints the return values.
3. If compilation fails with a syntax error, it attempts to compile the input string as-is (as a statement).
4. If this also fails with an "incomplete" error (e.g., "near <eof>"), it buffers the line and waits for more input.
