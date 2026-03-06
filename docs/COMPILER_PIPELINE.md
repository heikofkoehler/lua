# Compiler Pipeline

This document explains how Lua source code is transformed into executable bytecode.

## 1. Lexical Analysis (`Lexer`)
Found in `src/compiler/lexer.cpp/hpp`.
The lexer converts the raw source string into a stream of `Token` objects.
- **Features**: Supports long brackets `[[ ]]`, long comments `--[[ ]]`, hex floats, and all standard Lua escape sequences.
- **Interning**: String literals are interned early to ensure efficient comparison.

## 2. Parsing (`Parser`)
Found in `src/compiler/parser.cpp/hpp`.
The parser uses **Recursive Descent** to transform the token stream into an **Abstract Syntax Tree (AST)**.
- **Top-Down**: Starts from the `ProgramNode` and recursively parses statements and expressions.
- **Precedence**: Expression parsing uses a Pratt-style or layered approach to correctly handle operator precedence.
- **Error Recovery**: Implements a `synchronize()` method to skip tokens after an error and attempt to find the next valid statement, allowing the reporting of multiple errors.

## 3. The AST (`ast.hpp`)
The AST is a hierarchy of nodes representing Lua constructs (e.g., `IfStmtNode`, `BinaryNode`, `CallExprNode`).
- **Memory Management**: Nodes are managed using `std::unique_ptr` for clean ownership.
- **Visitor Pattern**: AST nodes support the `ASTVisitor` interface, which is used by the code generator.

## 4. Code Generation (`CodeGenerator`)
Found in `src/compiler/codegen.cpp/hpp`.
The code generator implements `ASTVisitor` to traverse the AST and emit bytecode.
- **Opcodes**: Emits instructions defined in `opcode.hpp`.
- **Constant Pool**: Manages the deduplication of constants (numbers, strings) within a `Chunk`.
- **Labels and Jumps**: Handles jump distance calculation for control flow structures.
- **Variable Scoping**: Tracks local variables and upvalues to determine which `OP_GET_LOCAL` or `OP_GET_UPVALUE` instruction to emit.

## 5. Bytecode Serialization
The resulting `FunctionObject` (which contains the `Chunk` of bytecode) can be serialized to a binary format.
- **Format**: Similar to standard Lua `.luac`, preserving all constants, upvalue metadata, and debug info (line numbers).
- **Deserialization**: The VM can load these binary files directly using `FunctionObject::deserialize()`, skipping the compilation phase entirely.
