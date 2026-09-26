#include "lsp/analysis.hpp"
#include "compiler/lexer.hpp"
#include "compiler/parser.hpp"
#include "compiler/ast.hpp"
#include "common/common.hpp"
#include <algorithm>
#include <functional>
#include <sstream>
#include <cctype>

namespace lsp {

static std::map<std::string, StdLibDoc> s_stdLibDocs;
static std::map<std::string, std::string> s_keywordDocs;
static bool s_stdLibInitialized = false;

void DocumentAnalyzer::initStdLib() {
    if (s_stdLibInitialized) return;
    s_stdLibInitialized = true;

    // --- Base Library ---
    s_stdLibDocs["assert"] = {"assert(v [, message])", "assert(v [, message]) -> v, ...", "Raises an error if value `v` is nil or false. Otherwise returns all its arguments.", CompletionItemKind::Function};
    s_stdLibDocs["collectgarbage"] = {"collectgarbage([opt [, arg]])", "collectgarbage([opt [, arg]]) -> any", "Performs a garbage-collection cycle or queries GC statistics.", CompletionItemKind::Function};
    s_stdLibDocs["dofile"] = {"dofile([filename])", "dofile([filename]) -> any", "Opens the named file and executes its contents as a Lua chunk.", CompletionItemKind::Function};
    s_stdLibDocs["error"] = {"error(message [, level])", "error(message [, level]) -> void", "Terminates the last protected function called and returns `message` as the error object.", CompletionItemKind::Function};
    s_stdLibDocs["_G"] = {"_G", "_G: table", "A global variable (not a function) that holds the global environment.", CompletionItemKind::Variable};
    s_stdLibDocs["getmetatable"] = {"getmetatable(object)", "getmetatable(object) -> table or nil", "Returns the metatable of the given object, or nil if none.", CompletionItemKind::Function};
    s_stdLibDocs["ipairs"] = {"ipairs(t)", "ipairs(t) -> iterator, t, 0", "Returns three values: an iterator function, the table `t`, and 0 for sequential integer indexing.", CompletionItemKind::Function};
    s_stdLibDocs["load"] = {"load(chunk [, chunkname [, mode [, env]]])", "load(chunk [, chunkname [, mode [, env]]]) -> function or (nil, error)", "Loads a chunk into a function.", CompletionItemKind::Function};
    s_stdLibDocs["loadfile"] = {"loadfile([filename [, mode [, env]]])", "loadfile([filename [, mode [, env]]]) -> function or (nil, error)", "Loads a chunk from file without executing.", CompletionItemKind::Function};
    s_stdLibDocs["next"] = {"next(table [, index])", "next(table [, index]) -> next_index, value", "Allows traversing all fields of a table. First call uses nil as index.", CompletionItemKind::Function};
    s_stdLibDocs["pairs"] = {"pairs(t)", "pairs(t) -> iterator, t, nil", "Returns an iterator function to traverse all key-value pairs of table `t`.", CompletionItemKind::Function};
    s_stdLibDocs["pcall"] = {"pcall(f [, arg1, ...])", "pcall(f [, arg1, ...]) -> boolean, res1, ...", "Calls function `f` with given arguments in protected mode. Catches runtime errors.", CompletionItemKind::Function};
    s_stdLibDocs["print"] = {"print(...)", "print(...) -> void", "Receives any number of arguments and prints their values to stdout.", CompletionItemKind::Function};
    s_stdLibDocs["rawequal"] = {"rawequal(v1, v2)", "rawequal(v1, v2) -> boolean", "Checks whether `v1` is equal to `v2`, without invoking any metamethod.", CompletionItemKind::Function};
    s_stdLibDocs["rawget"] = {"rawget(table, index)", "rawget(table, index) -> any", "Gets the real value of `table[index]`, without invoking any metamethod.", CompletionItemKind::Function};
    s_stdLibDocs["rawlen"] = {"rawlen(v)", "rawlen(v) -> integer", "Returns the length of string or table `v`, without invoking any metamethod.", CompletionItemKind::Function};
    s_stdLibDocs["rawset"] = {"rawset(table, index, value)", "rawset(table, index, value) -> table", "Sets the real value of `table[index]` to `value`, without invoking any metamethod.", CompletionItemKind::Function};
    s_stdLibDocs["select"] = {"select(index, ...)", "select(index, ...) -> any", "If index is a number, returns all arguments after index. If '#', returns total argument count.", CompletionItemKind::Function};
    s_stdLibDocs["setmetatable"] = {"setmetatable(table, metatable)", "setmetatable(table, metatable) -> table", "Sets the metatable for the given table.", CompletionItemKind::Function};
    s_stdLibDocs["tonumber"] = {"tonumber(e [, base])", "tonumber(e [, base]) -> number or nil", "Tries to convert its argument to a number.", CompletionItemKind::Function};
    s_stdLibDocs["tostring"] = {"tostring(v)", "tostring(v) -> string", "Converts any Lua value to a string representation.", CompletionItemKind::Function};
    s_stdLibDocs["type"] = {"type(v)", "type(v) -> string", "Returns the type of value `v`: \"nil\", \"number\", \"string\", \"boolean\", \"table\", \"function\", \"thread\", or \"userdata\".", CompletionItemKind::Function};
    s_stdLibDocs["_VERSION"] = {"_VERSION", "_VERSION: string", "A global variable holding a string with current Lua version.", CompletionItemKind::Variable};
    s_stdLibDocs["warn"] = {"warn(msg1, ...)", "warn(msg1, ...) -> void", "Emits a warning with the concatenated given messages.", CompletionItemKind::Function};
    s_stdLibDocs["xpcall"] = {"xpcall(f, msgh [, arg1, ...])", "xpcall(f, msgh [, arg1, ...]) -> boolean, res1, ...", "Calls function `f` in protected mode with an explicit message/error handler `msgh`.", CompletionItemKind::Function};

    // --- Modules ---
    s_stdLibDocs["coroutine"] = {"coroutine", "coroutine: table", "Standard Lua library for coroutine and thread manipulation.", CompletionItemKind::Module};
    s_stdLibDocs["string"] = {"string", "string: table", "Standard Lua library for string manipulation and pattern matching.", CompletionItemKind::Module};
    s_stdLibDocs["table"] = {"table", "table: table", "Standard Lua library for table operations, sorting, and array manipulation.", CompletionItemKind::Module};
    s_stdLibDocs["math"] = {"math", "math: table", "Standard Lua library for mathematical functions, trigonometry, and pseudo-random numbers.", CompletionItemKind::Module};
    s_stdLibDocs["io"] = {"io", "io: table", "Standard Lua library for file input and output operations.", CompletionItemKind::Module};
    s_stdLibDocs["os"] = {"os", "os: table", "Standard Lua library for operating system facilities and time.", CompletionItemKind::Module};
    s_stdLibDocs["debug"] = {"debug", "debug: table", "Standard Lua library providing debug facilities and reflection.", CompletionItemKind::Module};
    s_stdLibDocs["utf8"] = {"utf8", "utf8: table", "Standard Lua library providing UTF-8 support and codepoint manipulation.", CompletionItemKind::Module};
    s_stdLibDocs["package"] = {"package", "package: table", "Standard Lua library for managing module loading and paths.", CompletionItemKind::Module};

    // --- coroutine.* ---
    s_stdLibDocs["coroutine.create"] = {"coroutine.create(f)", "coroutine.create(f) -> thread", "Creates a new coroutine with body function `f`. Returns the new thread.", CompletionItemKind::Method};
    s_stdLibDocs["coroutine.resume"] = {"coroutine.resume(co [, val1, ...])", "coroutine.resume(co [, val1, ...]) -> boolean, ...", "Starts or continues execution of coroutine `co`.", CompletionItemKind::Method};
    s_stdLibDocs["coroutine.yield"] = {"coroutine.yield(...)", "coroutine.yield(...) -> ...", "Suspends the execution of the calling coroutine.", CompletionItemKind::Method};
    s_stdLibDocs["coroutine.status"] = {"coroutine.status(co)", "coroutine.status(co) -> string", "Returns the status of coroutine `co`: \"running\", \"suspended\", \"normal\", or \"dead\".", CompletionItemKind::Method};
    s_stdLibDocs["coroutine.wrap"] = {"coroutine.wrap(f)", "coroutine.wrap(f) -> function", "Creates a function that resumes the created coroutine each time it is called.", CompletionItemKind::Method};
    s_stdLibDocs["coroutine.running"] = {"coroutine.running()", "coroutine.running() -> (thread, boolean)", "Returns the currently running coroutine plus a boolean indicating if it is the main thread.", CompletionItemKind::Method};
    s_stdLibDocs["coroutine.isyieldable"] = {"coroutine.isyieldable([co])", "coroutine.isyieldable([co]) -> boolean", "Returns true if coroutine `co` can yield.", CompletionItemKind::Method};
    s_stdLibDocs["coroutine.close"] = {"coroutine.close(co)", "coroutine.close(co) -> boolean, error", "Closes coroutine `co`, terminating it and freeing its resources.", CompletionItemKind::Method};

    // --- string.* ---
    s_stdLibDocs["string.byte"] = {"string.byte(s [, i [, j]])", "string.byte(s [, i [, j]]) -> integer, ...", "Returns internal numeric codes of character `s[i], s[i+1], ..., s[j]`.", CompletionItemKind::Method};
    s_stdLibDocs["string.char"] = {"string.char(...)", "string.char(...) -> string", "Converts integer arguments into their character equivalents and concatenates them.", CompletionItemKind::Method};
    s_stdLibDocs["string.dump"] = {"string.dump(f [, strip])", "string.dump(f [, strip]) -> binary string", "Returns a binary string containing bytecode representation of function `f`.", CompletionItemKind::Method};
    s_stdLibDocs["string.find"] = {"string.find(s, pattern [, init [, plain]])", "string.find(s, pattern [, init [, plain]]) -> start, finish, captures...", "Looks for first match of `pattern` in `s`.", CompletionItemKind::Method};
    s_stdLibDocs["string.format"] = {"string.format(formatstring, ...)", "string.format(formatstring, ...) -> string", "Returns formatted version of its variable number of arguments following sprintf rules.", CompletionItemKind::Method};
    s_stdLibDocs["string.gmatch"] = {"string.gmatch(s, pattern [, init])", "string.gmatch(s, pattern [, init]) -> iterator", "Returns an iterator function that captures all matches of `pattern` in `s`.", CompletionItemKind::Method};
    s_stdLibDocs["string.gsub"] = {"string.gsub(s, pattern, repl [, n])", "string.gsub(s, pattern, repl [, n]) -> string, count", "Replaces occurrences of `pattern` with `repl`.", CompletionItemKind::Method};
    s_stdLibDocs["string.len"] = {"string.len(s)", "string.len(s) -> integer", "Returns the byte length of string `s`.", CompletionItemKind::Method};
    s_stdLibDocs["string.lower"] = {"string.lower(s)", "string.lower(s) -> string", "Converts string uppercase characters to lowercase.", CompletionItemKind::Method};
    s_stdLibDocs["string.match"] = {"string.match(s, pattern [, init])", "string.match(s, pattern [, init]) -> captures...", "Extracts captures from first match of `pattern` in `s`.", CompletionItemKind::Method};
    s_stdLibDocs["string.pack"] = {"string.pack(fmt, v1, v2, ...)", "string.pack(fmt, v1, v2, ...) -> binary string", "Packs values into binary string according to format `fmt`.", CompletionItemKind::Method};
    s_stdLibDocs["string.packsize"] = {"string.packsize(fmt)", "string.packsize(fmt) -> integer", "Returns byte size of string produced by `string.pack(fmt, ...)`.", CompletionItemKind::Method};
    s_stdLibDocs["string.rep"] = {"string.rep(s, n [, sep])", "string.rep(s, n [, sep]) -> string", "Returns string `s` repeated `n` times separated by `sep`.", CompletionItemKind::Method};
    s_stdLibDocs["string.reverse"] = {"string.reverse(s)", "string.reverse(s) -> string", "Reverses the byte order of string `s`.", CompletionItemKind::Method};
    s_stdLibDocs["string.sub"] = {"string.sub(s, i [, j])", "string.sub(s, i [, j]) -> string", "Returns substring of `s` starting at index `i` and ending at index `j`.", CompletionItemKind::Method};
    s_stdLibDocs["string.unpack"] = {"string.unpack(fmt, s [, pos])", "string.unpack(fmt, s [, pos]) -> v1, v2, ..., next_pos", "Unpacks values packed into binary string `s`.", CompletionItemKind::Method};
    s_stdLibDocs["string.upper"] = {"string.upper(s)", "string.upper(s) -> string", "Converts string lowercase characters to uppercase.", CompletionItemKind::Method};

    // --- table.* ---
    s_stdLibDocs["table.concat"] = {"table.concat(list [, sep [, i [, j]]])", "table.concat(list [, sep [, i [, j]]]) -> string", "Concatenates elements of `list` separated by `sep`.", CompletionItemKind::Method};
    s_stdLibDocs["table.insert"] = {"table.insert(list, [pos,] value)", "table.insert(list, [pos,] value) -> void", "Inserts `value` into `list` at position `pos` (defaults to end of list).", CompletionItemKind::Method};
    s_stdLibDocs["table.move"] = {"table.move(a1, f, e, t [, a2])", "table.move(a1, f, e, t [, a2]) -> a2", "Moves elements from table `a1[f..e]` to `a2[t..t+e-f]`.", CompletionItemKind::Method};
    s_stdLibDocs["table.pack"] = {"table.pack(...)", "table.pack(...) -> table", "Packs all arguments into a new table with field 'n' containing count.", CompletionItemKind::Method};
    s_stdLibDocs["table.remove"] = {"table.remove(list [, pos])", "table.remove(list [, pos]) -> any", "Removes element at `pos` from `list` and returns its value.", CompletionItemKind::Method};
    s_stdLibDocs["table.sort"] = {"table.sort(list [, comp])", "table.sort(list [, comp]) -> void", "Sorts list elements in place using optional comparator `comp`.", CompletionItemKind::Method};
    s_stdLibDocs["table.unpack"] = {"table.unpack(list [, i [, j]])", "table.unpack(list [, i [, j]]) -> elements...", "Returns elements from `list[i]` to `list[j]` as multiple return values.", CompletionItemKind::Method};

    // --- math.* ---
    s_stdLibDocs["math.abs"] = {"math.abs(x)", "math.abs(x) -> number", "Returns the absolute value of `x`.", CompletionItemKind::Method};
    s_stdLibDocs["math.acos"] = {"math.acos(x)", "math.acos(x) -> number", "Returns the arc cosine of `x` in radians.", CompletionItemKind::Method};
    s_stdLibDocs["math.asin"] = {"math.asin(x)", "math.asin(x) -> number", "Returns the arc sine of `x` in radians.", CompletionItemKind::Method};
    s_stdLibDocs["math.atan"] = {"math.atan(y [, x])", "math.atan(y [, x]) -> number", "Returns the arc tangent of `y/x` in radians.", CompletionItemKind::Method};
    s_stdLibDocs["math.ceil"] = {"math.ceil(x)", "math.ceil(x) -> integer", "Returns smallest integer greater than or equal to `x`.", CompletionItemKind::Method};
    s_stdLibDocs["math.cos"] = {"math.cos(x)", "math.cos(x) -> number", "Returns cosine of `x` (assumed to be in radians).", CompletionItemKind::Method};
    s_stdLibDocs["math.deg"] = {"math.deg(x)", "math.deg(x) -> number", "Converts angle `x` from radians to degrees.", CompletionItemKind::Method};
    s_stdLibDocs["math.exp"] = {"math.exp(x)", "math.exp(x) -> number", "Returns `e^x`.", CompletionItemKind::Method};
    s_stdLibDocs["math.floor"] = {"math.floor(x)", "math.floor(x) -> integer", "Returns largest integer less than or equal to `x`.", CompletionItemKind::Method};
    s_stdLibDocs["math.fmod"] = {"math.fmod(x, y)", "math.fmod(x, y) -> number", "Returns remainder of division of `x` by `y`.", CompletionItemKind::Method};
    s_stdLibDocs["math.huge"] = {"math.huge", "math.huge: number", "Floating point value larger than any other numeric value (infinity).", CompletionItemKind::Constant};
    s_stdLibDocs["math.log"] = {"math.log(x [, base])", "math.log(x [, base]) -> number", "Returns logarithm of `x` with given base (default `e`).", CompletionItemKind::Method};
    s_stdLibDocs["math.max"] = {"math.max(x, ...)", "math.max(x, ...) -> number", "Returns maximum value among its arguments.", CompletionItemKind::Method};
    s_stdLibDocs["math.maxinteger"] = {"math.maxinteger", "math.maxinteger: integer", "Maximum integer value representable in Lua.", CompletionItemKind::Constant};
    s_stdLibDocs["math.min"] = {"math.min(x, ...)", "math.min(x, ...) -> number", "Returns minimum value among its arguments.", CompletionItemKind::Method};
    s_stdLibDocs["math.mininteger"] = {"math.mininteger", "math.mininteger: integer", "Minimum integer value representable in Lua.", CompletionItemKind::Constant};
    s_stdLibDocs["math.modf"] = {"math.modf(x)", "math.modf(x) -> (integer, float)", "Returns integral and fractional parts of `x`.", CompletionItemKind::Method};
    s_stdLibDocs["math.pi"] = {"math.pi", "math.pi: number", "The value of mathematical constant Pi.", CompletionItemKind::Constant};
    s_stdLibDocs["math.rad"] = {"math.rad(x)", "math.rad(x) -> number", "Converts angle `x` from degrees to radians.", CompletionItemKind::Method};
    s_stdLibDocs["math.random"] = {"math.random([m [, n]])", "math.random([m [, n]]) -> number or integer", "Generates pseudo-random numbers.", CompletionItemKind::Method};
    s_stdLibDocs["math.randomseed"] = {"math.randomseed([x [, y]])", "math.randomseed([x [, y]]) -> integer, integer", "Sets seeds for the pseudo-random generator.", CompletionItemKind::Method};
    s_stdLibDocs["math.sin"] = {"math.sin(x)", "math.sin(x) -> number", "Returns sine of `x` (assumed to be in radians).", CompletionItemKind::Method};
    s_stdLibDocs["math.sqrt"] = {"math.sqrt(x)", "math.sqrt(x) -> number", "Returns square root of `x`.", CompletionItemKind::Method};
    s_stdLibDocs["math.tan"] = {"math.tan(x)", "math.tan(x) -> number", "Returns tangent of `x` (assumed to be in radians).", CompletionItemKind::Method};
    s_stdLibDocs["math.tointeger"] = {"math.tointeger(x)", "math.tointeger(x) -> integer or nil", "Converts `x` to an integer if possible.", CompletionItemKind::Method};
    s_stdLibDocs["math.type"] = {"math.type(x)", "math.type(x) -> string or nil", "Returns \"integer\", \"float\", or nil if `x` is not a number.", CompletionItemKind::Method};
    s_stdLibDocs["math.ult"] = {"math.ult(m, n)", "math.ult(m, n) -> boolean", "Returns true if integer `m` is below `n` in unsigned comparison.", CompletionItemKind::Method};

    // --- io.* ---
    s_stdLibDocs["io.close"] = {"io.close([file])", "io.close([file]) -> boolean, error", "Closes given file (default standard output).", CompletionItemKind::Method};
    s_stdLibDocs["io.flush"] = {"io.flush()", "io.flush() -> boolean", "Flushes default output file.", CompletionItemKind::Method};
    s_stdLibDocs["io.input"] = {"io.input([file])", "io.input([file]) -> file", "Sets or gets default input file.", CompletionItemKind::Method};
    s_stdLibDocs["io.lines"] = {"io.lines([filename, ...])", "io.lines([filename, ...]) -> iterator", "Opens given file name in read mode and returns an iterator over lines.", CompletionItemKind::Method};
    s_stdLibDocs["io.open"] = {"io.open(filename [, mode])", "io.open(filename [, mode]) -> file or (nil, error)", "Opens a file in the given mode (\"r\", \"w\", \"a\", \"r+\", etc.).", CompletionItemKind::Method};
    s_stdLibDocs["io.output"] = {"io.output([file])", "io.output([file]) -> file", "Sets or gets default output file.", CompletionItemKind::Method};
    s_stdLibDocs["io.popen"] = {"io.popen(prog [, mode])", "io.popen(prog [, mode]) -> file or (nil, error)", "Starts program in a separate process and connects its input/output to a file handle.", CompletionItemKind::Method};
    s_stdLibDocs["io.read"] = {"io.read(...)", "io.read(...) -> any", "Reads from default input file using given format (\"*l\", \"*a\", \"*n\", etc.).", CompletionItemKind::Method};
    s_stdLibDocs["io.tmpfile"] = {"io.tmpfile()", "io.tmpfile() -> file", "Returns handle for a temporary file opened in update mode.", CompletionItemKind::Method};
    s_stdLibDocs["io.type"] = {"io.type(obj)", "io.type(obj) -> string or nil", "Returns \"file\", \"closed file\", or nil if `obj` is not a file handle.", CompletionItemKind::Method};
    s_stdLibDocs["io.write"] = {"io.write(...)", "io.write(...) -> file or (nil, error)", "Writes values to default output file.", CompletionItemKind::Method};
    s_stdLibDocs["io.stdin"] = {"io.stdin", "io.stdin: file", "Standard input file handle.", CompletionItemKind::Variable};
    s_stdLibDocs["io.stdout"] = {"io.stdout", "io.stdout: file", "Standard output file handle.", CompletionItemKind::Variable};
    s_stdLibDocs["io.stderr"] = {"io.stderr", "io.stderr: file", "Standard error file handle.", CompletionItemKind::Variable};

    // --- os.* ---
    s_stdLibDocs["os.clock"] = {"os.clock()", "os.clock() -> number", "Returns approximation of CPU time used by the program in seconds.", CompletionItemKind::Method};
    s_stdLibDocs["os.date"] = {"os.date([format [, time]])", "os.date([format [, time]]) -> string or table", "Returns string or table containing date and time formatted according to `format`.", CompletionItemKind::Method};
    s_stdLibDocs["os.difftime"] = {"os.difftime(t2, t1)", "os.difftime(t2, t1) -> number", "Returns difference in seconds between two times `t2` and `t1`.", CompletionItemKind::Method};
    s_stdLibDocs["os.execute"] = {"os.execute([command])", "os.execute([command]) -> boolean, string, integer", "Executes an operating system shell command.", CompletionItemKind::Method};
    s_stdLibDocs["os.exit"] = {"os.exit([code [, close]])", "os.exit([code [, close]]) -> void", "Terminates the host program with exit status `code`.", CompletionItemKind::Method};
    s_stdLibDocs["os.getenv"] = {"os.getenv(varname)", "os.getenv(varname) -> string or nil", "Returns value of process environment variable `varname`.", CompletionItemKind::Method};
    s_stdLibDocs["os.remove"] = {"os.remove(filename)", "os.remove(filename) -> boolean or (nil, error)", "Deletes file with given name.", CompletionItemKind::Method};
    s_stdLibDocs["os.rename"] = {"os.rename(oldname, newname)", "os.rename(oldname, newname) -> boolean or (nil, error)", "Renames file or directory `oldname` to `newname`.", CompletionItemKind::Method};
    s_stdLibDocs["os.setlocale"] = {"os.setlocale(locale [, category])", "os.setlocale(locale [, category]) -> string or nil", "Sets the current locale of the program.", CompletionItemKind::Method};
    s_stdLibDocs["os.time"] = {"os.time([table])", "os.time([table]) -> integer", "Returns current time or time representing fields in `table`.", CompletionItemKind::Method};
    s_stdLibDocs["os.tmpname"] = {"os.tmpname()", "os.tmpname() -> string", "Returns string with file name suitable for temporary file.", CompletionItemKind::Method};

    // --- utf8.* ---
    s_stdLibDocs["utf8.char"] = {"utf8.char(...)", "utf8.char(...) -> string", "Receives zero or more integers, converts each to its UTF-8 sequence, and returns string.", CompletionItemKind::Method};
    s_stdLibDocs["utf8.charpattern"] = {"utf8.charpattern", "utf8.charpattern: string", "Pattern matching exactly one UTF-8 byte sequence.", CompletionItemKind::Constant};
    s_stdLibDocs["utf8.codes"] = {"utf8.codes(s [, lax])", "utf8.codes(s [, lax]) -> iterator", "Returns iterator values so loop `for p, c in utf8.codes(s)` iterates over all codepoints.", CompletionItemKind::Method};
    s_stdLibDocs["utf8.codepoint"] = {"utf8.codepoint(s [, i [, j [, lax]]])", "utf8.codepoint(s [, i [, j [, lax]]]) -> codepoints...", "Returns codepoints from `s[i..j]` as integers.", CompletionItemKind::Method};
    s_stdLibDocs["utf8.len"] = {"utf8.len(s [, i [, j [, lax]]])", "utf8.len(s [, i [, j [, lax]]]) -> integer or (nil, pos)", "Returns number of UTF-8 characters in `s[i..j]`.", CompletionItemKind::Method};
    s_stdLibDocs["utf8.offset"] = {"utf8.offset(s, n [, i])", "utf8.offset(s, n [, i]) -> integer", "Returns position in `s` where encoding of the `n`-th character starts.", CompletionItemKind::Method};

    // --- package.* ---
    s_stdLibDocs["package.config"] = {"package.config", "package.config: string", "String describing compile-time configurations for packages.", CompletionItemKind::Constant};
    s_stdLibDocs["package.cpath"] = {"package.cpath", "package.cpath: string", "Path used by `require` to search for C module libraries.", CompletionItemKind::Variable};
    s_stdLibDocs["package.loaded"] = {"package.loaded", "package.loaded: table", "Table used by `require` to control which modules are already loaded.", CompletionItemKind::Variable};
    s_stdLibDocs["package.loadlib"] = {"package.loadlib(libname, funcname)", "package.loadlib(libname, funcname) -> function or (nil, error)", "Dynamically links host program with C library `libname`.", CompletionItemKind::Method};
    s_stdLibDocs["package.path"] = {"package.path", "package.path: string", "Path used by `require` to search for Lua module files.", CompletionItemKind::Variable};
    s_stdLibDocs["package.preload"] = {"package.preload", "package.preload: table", "Table storing loaders for specific modules.", CompletionItemKind::Variable};
    s_stdLibDocs["package.searchers"] = {"package.searchers", "package.searchers: table", "Table of searcher functions used by `require`.", CompletionItemKind::Variable};
    s_stdLibDocs["package.searchpath"] = {"package.searchpath(name, path [, sep [, rep]])", "package.searchpath(name, path [, sep [, rep]]) -> filename or (nil, error)", "Searches for given `name` in specified path template.", CompletionItemKind::Method};

    // --- Keywords ---
    s_keywordDocs["and"] = "Logical operator: Returns its first argument if it is false or nil; otherwise returns its second argument.";
    s_keywordDocs["break"] = "Control structure: Terminates execution of a `while`, `repeat`, or `for` loop.";
    s_keywordDocs["do"] = "Block statement: Introduces a new explicit lexical scope block (`do ... end`).";
    s_keywordDocs["else"] = "Conditional branch: Executes statements when all preceding `if` and `elseif` conditions are false.";
    s_keywordDocs["elseif"] = "Conditional branch: Tests an alternative condition when previous conditions are false.";
    s_keywordDocs["end"] = "Delimiter: Terminates a `do`, `if`, `while`, `for`, or `function` block.";
    s_keywordDocs["false"] = "Boolean literal representing logical falsity.";
    s_keywordDocs["for"] = "Loop statement: Numeric (`for i = 1, 10 do ... end`) or generic (`for k, v in pairs(t) do ... end`).";
    s_keywordDocs["function"] = "Defines a named or anonymous function closure.";
    s_keywordDocs["global"] = "Lua 5.5 keyword: Declares explicit global variables and functions.";
    s_keywordDocs["goto"] = "Jump statement: Transfers control unconditionally to a label (`::name::`).";
    s_keywordDocs["if"] = "Conditional statement: Executes body if condition evaluates to true (not false or nil).";
    s_keywordDocs["in"] = "Used in generic `for` loops to specify the iterator expressions.";
    s_keywordDocs["local"] = "Variable declaration: Declares local variables or local functions scoped to the current block.";
    s_keywordDocs["nil"] = "Represents the absence of a useful value; non-existent table fields evaluate to nil.";
    s_keywordDocs["not"] = "Unary logical operator: Inverts truth value (returns true for nil and false, false otherwise).";
    s_keywordDocs["or"] = "Logical operator: Returns its first argument if not false or nil; otherwise returns its second argument.";
    s_keywordDocs["repeat"] = "Loop statement: Executes body until the trailing `until` condition becomes true.";
    s_keywordDocs["return"] = "Statement: Returns zero or more values from the enclosing function.";
    s_keywordDocs["then"] = "Delimiter following the condition in an `if` or `elseif` statement.";
    s_keywordDocs["true"] = "Boolean literal representing logical truth.";
    s_keywordDocs["until"] = "Delimiter terminating a `repeat ... until <condition>` loop.";
    s_keywordDocs["while"] = "Loop statement: Executes body repeatedly as long as the condition evaluates to true.";
}

const std::map<std::string, StdLibDoc>& DocumentAnalyzer::getStdLibDocs() {
    initStdLib();
    return s_stdLibDocs;
}

const std::map<std::string, std::string>& DocumentAnalyzer::getKeywordDocs() {
    initStdLib();
    return s_keywordDocs;
}

// AST Visitor to collect document symbols and build scope hierarchy
class LspSymbolVisitor : public ASTVisitor {
public:
    LspSymbolVisitor(DocumentAnalyzer* doc, std::shared_ptr<Scope> rootScope)
        : doc_(doc), currentScope_(rootScope) {}

    void visitProgram(ProgramNode* node) override {
        for (const auto& stmt : node->statements()) {
            if (stmt) stmt->accept(*this);
        }
    }

    void visitFunctionDecl(FunctionDeclNode* node) override {
        int line = std::max(0, node->line() - 1);
        int lastLine = std::max(line, node->lastLine() - 1);
        std::string lineStr = doc_->getLine(line);

        std::string sig = "function " + node->name() + "(";
        for (size_t i = 0; i < node->params().size(); ++i) {
            if (i > 0) sig += ", ";
            sig += node->params()[i];
        }
        if (node->hasVarargs()) {
            if (!node->params().empty()) sig += ", ";
            sig += node->hasNamedVarargs() ? ("..." + node->varargName()) : "...";
        }
        sig += ")";

        Range range{{line, 0}, {lastLine, static_cast<int>(doc_->getLine(lastLine).length())}};
        int nameStart = static_cast<int>(lineStr.find(node->name()));
        if (nameStart < 0) nameStart = 0;
        Range selRange{{line, nameStart}, {line, nameStart + static_cast<int>(node->name().length())}};

        ScopeSymbol sym;
        sym.name = node->name();
        sym.detail = sig;
        sym.doc = doc_->extractPrecedingComment(line);
        sym.kind = SymbolKind::Function;
        sym.range = range;
        sym.selectionRange = selRange;
        sym.lineDefined = line;
        currentScope_->symbols.push_back(sym);

        DocumentSymbol docSym;
        docSym.name = node->name();
        docSym.detail = sig;
        docSym.kind = SymbolKind::Function;
        docSym.range = range;
        docSym.selectionRange = selRange;

        // Enter function body scope
        auto childScope = std::make_shared<Scope>();
        childScope->id = ++scopeCounter_;
        childScope->startLine = line;
        childScope->endLine = lastLine;
        childScope->parent = currentScope_;
        currentScope_->children.push_back(childScope);

        // Add parameters to scope
        for (const auto& param : node->params()) {
            ScopeSymbol pSym;
            pSym.name = param;
            pSym.detail = "parameter " + param;
            pSym.kind = SymbolKind::Variable;
            pSym.range = selRange;
            pSym.selectionRange = selRange;
            pSym.lineDefined = line;
            childScope->symbols.push_back(pSym);
        }

        auto prevScope = currentScope_;
        currentScope_ = childScope;
        for (const auto& stmt : node->body()) {
            if (stmt) stmt->accept(*this);
        }
        currentScope_ = prevScope;

        docSymbols_.push_back(docSym);
    }

    void visitLocalDeclStmt(LocalDeclStmtNode* node) override {
        int line = std::max(0, node->line() - 1);
        std::string lineStr = doc_->getLine(line);
        int nameStart = static_cast<int>(lineStr.find(node->name()));
        if (nameStart < 0) nameStart = 0;
        Range selRange{{line, nameStart}, {line, nameStart + static_cast<int>(node->name().length())}};
        Range range{{line, 0}, {line, static_cast<int>(lineStr.length())}};

        ScopeSymbol sym;
        sym.name = node->name();
        sym.kind = node->isFunction() ? SymbolKind::Function : SymbolKind::Variable;
        sym.detail = node->isFunction() ? ("local function " + node->name()) : ("local " + node->name());
        sym.doc = doc_->extractPrecedingComment(line);
        sym.range = range;
        sym.selectionRange = selRange;
        sym.lineDefined = line;
        currentScope_->symbols.push_back(sym);

        DocumentSymbol docSym;
        docSym.name = node->name();
        docSym.detail = sym.detail;
        docSym.kind = sym.kind;
        docSym.range = range;
        docSym.selectionRange = selRange;
        docSymbols_.push_back(docSym);

        if (node->initializer()) {
            node->initializer()->accept(*this);
        }
    }

    void visitMultipleLocalDeclStmt(MultipleLocalDeclStmtNode* node) override {
        int line = std::max(0, node->line() - 1);
        std::string lineStr = doc_->getLine(line);

        for (const auto& v : node->vars()) {
            int nameStart = static_cast<int>(lineStr.find(v.name));
            if (nameStart < 0) nameStart = 0;
            Range selRange{{line, nameStart}, {line, nameStart + static_cast<int>(v.name.length())}};
            Range range{{line, 0}, {line, static_cast<int>(lineStr.length())}};

            ScopeSymbol sym;
            sym.name = v.name;
            sym.kind = SymbolKind::Variable;
            sym.detail = "local " + v.name;
            sym.doc = doc_->extractPrecedingComment(line);
            sym.range = range;
            sym.selectionRange = selRange;
            sym.lineDefined = line;
            currentScope_->symbols.push_back(sym);

            DocumentSymbol docSym;
            docSym.name = v.name;
            docSym.detail = sym.detail;
            docSym.kind = SymbolKind::Variable;
            docSym.range = range;
            docSym.selectionRange = selRange;
            docSymbols_.push_back(docSym);
        }

        for (const auto& init : node->initializers()) {
            if (init) init->accept(*this);
        }
    }

    void visitGlobalDeclStmt(GlobalDeclStmtNode* node) override {
        int line = std::max(0, node->line() - 1);
        std::string lineStr = doc_->getLine(line);
        int nameStart = static_cast<int>(lineStr.find(node->name()));
        if (nameStart < 0) nameStart = 0;
        Range selRange{{line, nameStart}, {line, nameStart + static_cast<int>(node->name().length())}};
        Range range{{line, 0}, {line, static_cast<int>(lineStr.length())}};

        ScopeSymbol sym;
        sym.name = node->name();
        sym.kind = node->isFunction() ? SymbolKind::Function : SymbolKind::Variable;
        sym.detail = "global " + node->name();
        sym.doc = doc_->extractPrecedingComment(line);
        sym.range = range;
        sym.selectionRange = selRange;
        sym.lineDefined = line;
        currentScope_->symbols.push_back(sym);

        DocumentSymbol docSym;
        docSym.name = node->name();
        docSym.detail = sym.detail;
        docSym.kind = sym.kind;
        docSym.range = range;
        docSym.selectionRange = selRange;
        docSymbols_.push_back(docSym);

        if (node->initializer()) node->initializer()->accept(*this);
    }

    void visitMultipleGlobalDeclStmt(MultipleGlobalDeclStmtNode* node) override {
        int line = std::max(0, node->line() - 1);
        std::string lineStr = doc_->getLine(line);
        for (const auto& v : node->vars()) {
            int nameStart = static_cast<int>(lineStr.find(v.name));
            if (nameStart < 0) nameStart = 0;
            Range selRange{{line, nameStart}, {line, nameStart + static_cast<int>(v.name.length())}};
            Range range{{line, 0}, {line, static_cast<int>(lineStr.length())}};

            ScopeSymbol sym;
            sym.name = v.name;
            sym.kind = SymbolKind::Variable;
            sym.detail = "global " + v.name;
            sym.range = range;
            sym.selectionRange = selRange;
            sym.lineDefined = line;
            currentScope_->symbols.push_back(sym);

            DocumentSymbol docSym;
            docSym.name = v.name;
            docSym.detail = sym.detail;
            docSym.kind = SymbolKind::Variable;
            docSym.range = range;
            docSym.selectionRange = selRange;
            docSymbols_.push_back(docSym);
        }
    }

    void visitAssignmentStmt(AssignmentStmtNode* node) override {
        int line = std::max(0, node->line() - 1);
        std::string lineStr = doc_->getLine(line);

        if (auto* funcExpr = dynamic_cast<FunctionExprNode*>(node->value())) {
            int lastLine = std::max(line, funcExpr->lastLineDefined() - 1);

            std::string sig = "function " + node->name() + "(";
            for (size_t i = 0; i < funcExpr->params().size(); ++i) {
                if (i > 0) sig += ", ";
                sig += funcExpr->params()[i];
            }
            if (funcExpr->hasVarargs()) {
                if (!funcExpr->params().empty()) sig += ", ";
                sig += funcExpr->hasNamedVarargs() ? ("..." + funcExpr->varargName()) : "...";
            }
            sig += ")";

            Range range{{line, 0}, {lastLine, static_cast<int>(doc_->getLine(lastLine).length())}};
            int nameStart = static_cast<int>(lineStr.find(node->name()));
            if (nameStart < 0) nameStart = 0;
            Range selRange{{line, nameStart}, {line, nameStart + static_cast<int>(node->name().length())}};

            ScopeSymbol sym;
            sym.name = node->name();
            sym.detail = sig;
            sym.doc = doc_->extractPrecedingComment(line);
            sym.kind = SymbolKind::Function;
            sym.range = range;
            sym.selectionRange = selRange;
            sym.lineDefined = line;
            currentScope_->symbols.push_back(sym);

            DocumentSymbol docSym;
            docSym.name = node->name();
            docSym.detail = sig;
            docSym.kind = SymbolKind::Function;
            docSym.range = range;
            docSym.selectionRange = selRange;
            docSymbols_.push_back(docSym);

            // Function scope
            auto childScope = std::make_shared<Scope>();
            childScope->id = ++scopeCounter_;
            childScope->startLine = line;
            childScope->endLine = lastLine;
            childScope->parent = currentScope_;
            currentScope_->children.push_back(childScope);

            for (const auto& param : funcExpr->params()) {
                ScopeSymbol pSym;
                pSym.name = param;
                pSym.detail = "parameter " + param;
                pSym.kind = SymbolKind::Variable;
                pSym.range = selRange;
                pSym.selectionRange = selRange;
                pSym.lineDefined = line;
                childScope->symbols.push_back(pSym);
            }

            auto prevScope = currentScope_;
            currentScope_ = childScope;
            for (const auto& s : funcExpr->body()) {
                if (s) s->accept(*this);
            }
            currentScope_ = prevScope;
            return;
        }

        // Regular assignment
        int nameStart = static_cast<int>(lineStr.find(node->name()));
        if (nameStart < 0) nameStart = 0;
        Range selRange{{line, nameStart}, {line, nameStart + static_cast<int>(node->name().length())}};
        Range range{{line, 0}, {line, static_cast<int>(lineStr.length())}};

        ScopeSymbol sym;
        sym.name = node->name();
        sym.kind = SymbolKind::Variable;
        sym.detail = node->name();
        sym.doc = doc_->extractPrecedingComment(line);
        sym.range = range;
        sym.selectionRange = selRange;
        sym.lineDefined = line;
        currentScope_->symbols.push_back(sym);

        DocumentSymbol docSym;
        docSym.name = node->name();
        docSym.detail = sym.detail;
        docSym.kind = SymbolKind::Variable;
        docSym.range = range;
        docSym.selectionRange = selRange;
        docSymbols_.push_back(docSym);

        if (node->value()) node->value()->accept(*this);
    }

    void visitIndexAssignmentStmt(IndexAssignmentStmtNode* node) override {
        int line = std::max(0, node->line() - 1);
        std::string lineStr = doc_->getLine(line);

        if (auto* funcExpr = dynamic_cast<FunctionExprNode*>(node->value())) {
            std::string keyName = "method";
            if (auto* strKey = dynamic_cast<StringLiteralNode*>(node->key())) {
                keyName = strKey->content();
            }

            int lastLine = std::max(line, funcExpr->lastLineDefined() - 1);
            std::string sig = "function " + keyName + "(";
            for (size_t i = 0; i < funcExpr->params().size(); ++i) {
                if (i > 0) sig += ", ";
                sig += funcExpr->params()[i];
            }
            if (funcExpr->hasVarargs()) {
                if (!funcExpr->params().empty()) sig += ", ";
                sig += funcExpr->hasNamedVarargs() ? ("..." + funcExpr->varargName()) : "...";
            }
            sig += ")";

            Range range{{line, 0}, {lastLine, static_cast<int>(doc_->getLine(lastLine).length())}};
            int nameStart = static_cast<int>(lineStr.find(keyName));
            if (nameStart < 0) nameStart = 0;
            Range selRange{{line, nameStart}, {line, nameStart + static_cast<int>(keyName.length())}};

            ScopeSymbol sym;
            sym.name = keyName;
            sym.detail = sig;
            sym.doc = doc_->extractPrecedingComment(line);
            sym.kind = SymbolKind::Method;
            sym.range = range;
            sym.selectionRange = selRange;
            sym.lineDefined = line;
            currentScope_->symbols.push_back(sym);

            DocumentSymbol docSym;
            docSym.name = keyName;
            docSym.detail = sig;
            docSym.kind = SymbolKind::Method;
            docSym.range = range;
            docSym.selectionRange = selRange;
            docSymbols_.push_back(docSym);

            auto childScope = std::make_shared<Scope>();
            childScope->id = ++scopeCounter_;
            childScope->startLine = line;
            childScope->endLine = lastLine;
            childScope->parent = currentScope_;
            currentScope_->children.push_back(childScope);

            for (const auto& param : funcExpr->params()) {
                ScopeSymbol pSym;
                pSym.name = param;
                pSym.detail = "parameter " + param;
                pSym.kind = SymbolKind::Variable;
                pSym.range = selRange;
                pSym.selectionRange = selRange;
                pSym.lineDefined = line;
                childScope->symbols.push_back(pSym);
            }

            auto prevScope = currentScope_;
            currentScope_ = childScope;
            for (const auto& s : funcExpr->body()) {
                if (s) s->accept(*this);
            }
            currentScope_ = prevScope;
            return;
        }

        if (node->table()) node->table()->accept(*this);
        if (node->key()) node->key()->accept(*this);
        if (node->value()) node->value()->accept(*this);
    }

    void visitMultipleAssignmentStmt(MultipleAssignmentStmtNode* node) override {
        for (const auto& v : node->values()) {
            if (v) v->accept(*this);
        }
    }

    void visitIfStmt(IfStmtNode* node) override {
        if (node->condition()) node->condition()->accept(*this);
        for (const auto& s : node->thenBranch()) {
            if (s) s->accept(*this);
        }
        for (const auto& b : node->elseIfBranches()) {
            if (b.condition) b.condition->accept(*this);
            for (const auto& s : b.body) {
                if (s) s->accept(*this);
            }
        }
        for (const auto& s : node->elseBranch()) {
            if (s) s->accept(*this);
        }
    }

    void visitWhileStmt(WhileStmtNode* node) override {
        if (node->condition()) node->condition()->accept(*this);
        for (const auto& s : node->body()) {
            if (s) s->accept(*this);
        }
    }

    void visitRepeatStmt(RepeatStmtNode* node) override {
        for (const auto& s : node->body()) {
            if (s) s->accept(*this);
        }
        if (node->condition()) node->condition()->accept(*this);
    }

    void visitForStmt(ForStmtNode* node) override {
        int line = std::max(0, node->line() - 1);
        int endLine = std::max(line, node->endLine() - 1);
        std::string lineStr = doc_->getLine(line);

        auto childScope = std::make_shared<Scope>();
        childScope->id = ++scopeCounter_;
        childScope->startLine = line;
        childScope->endLine = endLine;
        childScope->parent = currentScope_;
        currentScope_->children.push_back(childScope);

        ScopeSymbol varSym;
        varSym.name = node->varName();
        varSym.kind = SymbolKind::Variable;
        varSym.detail = "for loop variable " + node->varName();
        varSym.lineDefined = line;
        childScope->symbols.push_back(varSym);

        auto prevScope = currentScope_;
        currentScope_ = childScope;
        for (const auto& s : node->body()) {
            if (s) s->accept(*this);
        }
        currentScope_ = prevScope;
    }

    void visitForInStmt(ForInStmtNode* node) override {
        int line = std::max(0, node->line() - 1);
        int endLine = std::max(line, node->endLine() - 1);

        auto childScope = std::make_shared<Scope>();
        childScope->id = ++scopeCounter_;
        childScope->startLine = line;
        childScope->endLine = endLine;
        childScope->parent = currentScope_;
        currentScope_->children.push_back(childScope);

        for (const auto& name : node->varNames()) {
            ScopeSymbol varSym;
            varSym.name = name;
            varSym.kind = SymbolKind::Variable;
            varSym.detail = "for-in loop variable " + name;
            varSym.lineDefined = line;
            childScope->symbols.push_back(varSym);
        }

        auto prevScope = currentScope_;
        currentScope_ = childScope;
        for (const auto& s : node->body()) {
            if (s) s->accept(*this);
        }
        currentScope_ = prevScope;
    }

    void visitBlock(BlockStmtNode* node) override {
        int line = std::max(0, node->line() - 1);
        int lastLine = std::max(line, node->lastLine() - 1);

        auto childScope = std::make_shared<Scope>();
        childScope->id = ++scopeCounter_;
        childScope->startLine = line;
        childScope->endLine = lastLine;
        childScope->parent = currentScope_;
        currentScope_->children.push_back(childScope);

        auto prevScope = currentScope_;
        currentScope_ = childScope;
        for (const auto& s : node->statements()) {
            if (s) s->accept(*this);
        }
        currentScope_ = prevScope;
    }

    void visitLabel(LabelStmtNode* node) override {
        int line = std::max(0, node->line() - 1);
        std::string lineStr = doc_->getLine(line);
        Range range{{line, 0}, {line, static_cast<int>(lineStr.length())}};

        ScopeSymbol sym;
        sym.name = node->label();
        sym.kind = SymbolKind::Constant;
        sym.detail = "label ::" + node->label() + "::";
        sym.range = range;
        sym.selectionRange = range;
        sym.lineDefined = line;
        currentScope_->symbols.push_back(sym);
    }

    void visitFunctionExpr(FunctionExprNode* node) override {
        int line = std::max(0, node->lineDefined() - 1);
        int lastLine = std::max(line, node->lastLineDefined() - 1);

        auto childScope = std::make_shared<Scope>();
        childScope->id = ++scopeCounter_;
        childScope->startLine = line;
        childScope->endLine = lastLine;
        childScope->parent = currentScope_;
        currentScope_->children.push_back(childScope);

        for (const auto& param : node->params()) {
            ScopeSymbol pSym;
            pSym.name = param;
            pSym.detail = "parameter " + param;
            pSym.kind = SymbolKind::Variable;
            pSym.lineDefined = line;
            childScope->symbols.push_back(pSym);
        }

        auto prevScope = currentScope_;
        currentScope_ = childScope;
        for (const auto& s : node->body()) {
            if (s) s->accept(*this);
        }
        currentScope_ = prevScope;
    }

    void visitLiteral(LiteralNode*) override {}
    void visitStringLiteral(StringLiteralNode*) override {}
    void visitUnary(UnaryNode* node) override { if (node->operand()) node->operand()->accept(*this); }
    void visitBinary(BinaryNode* node) override {
        if (node->left()) node->left()->accept(*this);
        if (node->right()) node->right()->accept(*this);
    }
    void visitVariable(VariableExprNode*) override {}
    void visitVararg(VarargExprNode*) override {}
    void visitCall(CallExprNode* node) override {
        if (node->callee()) node->callee()->accept(*this);
        for (const auto& a : node->args()) { if (a) a->accept(*this); }
    }
    void visitMethodCall(MethodCallExprNode* node) override {
        if (node->object()) node->object()->accept(*this);
        for (const auto& a : node->args()) { if (a) a->accept(*this); }
    }
    void visitTableConstructor(TableConstructorNode* node) override {
        for (const auto& e : node->entries()) {
            if (e.key) e.key->accept(*this);
            if (e.value) e.value->accept(*this);
        }
    }
    void visitIndexExpr(IndexExprNode* node) override {
        if (node->table()) node->table()->accept(*this);
        if (node->key()) node->key()->accept(*this);
    }
    void visitGroupExpr(GroupExprNode* node) override {
        if (node->expr()) node->expr()->accept(*this);
    }
    void visitExprStmt(ExprStmtNode* node) override {
        if (node->expr()) node->expr()->accept(*this);
    }
    void visitReturn(ReturnStmtNode* node) override {
        for (const auto& v : node->values()) { if (v) v->accept(*this); }
    }
    void visitBreak(BreakStmtNode*) override {}
    void visitGoto(GotoStmtNode*) override {}

    const std::vector<DocumentSymbol>& documentSymbols() const { return docSymbols_; }

private:
    DocumentAnalyzer* doc_;
    std::shared_ptr<Scope> currentScope_;
    std::vector<DocumentSymbol> docSymbols_;
    int scopeCounter_ = 0;
};

// DocumentAnalyzer Implementation
DocumentAnalyzer::DocumentAnalyzer(std::string uri, std::string text, int version)
    : uri_(std::move(uri)), text_(std::move(text)), version_(version) {
    initStdLib();
    rebuildLineStarts();
    analyze();
}

void DocumentAnalyzer::update(std::string text, int version) {
    text_ = std::move(text);
    version_ = version;
    rebuildLineStarts();
    analyze();
}

void DocumentAnalyzer::rebuildLineStarts() {
    lineStarts_.clear();
    lineStarts_.push_back(0);
    for (size_t i = 0; i < text_.size(); ++i) {
        if (text_[i] == '\n') {
            lineStarts_.push_back(i + 1);
        }
    }
}

int DocumentAnalyzer::lineCount() const {
    return static_cast<int>(lineStarts_.size());
}

std::string DocumentAnalyzer::getLine(int line) const {
    if (line < 0 || line >= static_cast<int>(lineStarts_.size())) return "";
    size_t start = lineStarts_[line];
    size_t end = (line + 1 < static_cast<int>(lineStarts_.size())) ? lineStarts_[line + 1] - 1 : text_.size();
    if (end > start && text_[end - 1] == '\r') end--;
    if (start > end) return "";
    return text_.substr(start, end - start);
}

Position DocumentAnalyzer::offsetToPosition(size_t offset) const {
    Position pos;
    if (offset >= text_.size()) offset = text_.size();
    auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset);
    if (it != lineStarts_.begin()) {
        --it;
        pos.line = static_cast<int>(std::distance(lineStarts_.begin(), it));
        pos.character = static_cast<int>(offset - *it);
    }
    return pos;
}

size_t DocumentAnalyzer::positionToOffset(Position pos) const {
    if (pos.line < 0) return 0;
    if (pos.line >= static_cast<int>(lineStarts_.size())) return text_.size();
    size_t lineStart = lineStarts_[pos.line];
    size_t lineLen = getLine(pos.line).length();
    size_t charOffset = static_cast<size_t>(std::max(0, pos.character));
    if (charOffset > lineLen) charOffset = lineLen;
    return lineStart + charOffset;
}

std::string DocumentAnalyzer::extractPrecedingComment(int line) const {
    std::string comment;
    int checkLine = line - 1;
    std::vector<std::string> commentLines;
    while (checkLine >= 0) {
        std::string l = getLine(checkLine);
        size_t firstNonWs = l.find_first_not_of(" \t");
        if (firstNonWs != std::string::npos && l.substr(firstNonWs, 2) == "--") {
            std::string content = l.substr(firstNonWs + 2);
            if (!content.empty() && content[0] == ' ') content.erase(0, 1);
            commentLines.push_back(content);
            checkLine--;
        } else {
            break;
        }
    }
    std::reverse(commentLines.begin(), commentLines.end());
    for (size_t i = 0; i < commentLines.size(); ++i) {
        if (i > 0) comment += "\n";
        comment += commentLines[i];
    }
    return comment;
}

void DocumentAnalyzer::analyze() {
    diagnostics_.clear();
    symbols_.clear();
    rootScope_ = std::make_shared<Scope>();
    rootScope_->startLine = 0;
    rootScope_->endLine = std::max(0, lineCount() - 1);

    try {
        Lexer lexer(text_);
        lexer.setSourceName(uri_);
        Parser parser(lexer);
        auto program = parser.parse();
        if (program) {
            LspSymbolVisitor visitor(this, rootScope_);
            program->accept(visitor);
            symbols_ = visitor.documentSymbols();
        }
    } catch (const CompileError& err) {
        std::string errStr = err.what();
        int errLine = 1;
        std::string cleanMsg = errStr;

        size_t firstColon = errStr.find(':');
        if (firstColon != std::string::npos) {
            size_t secondColon = errStr.find(':', firstColon + 1);
            if (secondColon != std::string::npos) {
                std::string lineStr = errStr.substr(firstColon + 1, secondColon - firstColon - 1);
                try {
                    errLine = std::stoi(lineStr);
                } catch (...) {}
                cleanMsg = errStr.substr(secondColon + 1);
                while (!cleanMsg.empty() && cleanMsg[0] == ' ') cleanMsg.erase(0, 1);
            }
        }

        int lspLine = std::max(0, errLine - 1);
        std::string lineText = getLine(lspLine);
        int startChar = 0;
        int endChar = static_cast<int>(lineText.length());

        size_t nearPos = cleanMsg.find("near '");
        if (nearPos != std::string::npos) {
            size_t tokenStart = nearPos + 6;
            size_t tokenEnd = cleanMsg.find("'", tokenStart);
            if (tokenEnd != std::string::npos) {
                std::string tokenLexeme = cleanMsg.substr(tokenStart, tokenEnd - tokenStart);
                size_t found = lineText.find(tokenLexeme);
                if (found != std::string::npos) {
                    startChar = static_cast<int>(found);
                    endChar = static_cast<int>(found + tokenLexeme.length());
                }
            }
        }

        Diagnostic diag;
        diag.range.start = {lspLine, startChar};
        diag.range.end = {lspLine, std::max(startChar + 1, endChar)};
        diag.severity = DiagnosticSeverity::Error;
        diag.message = cleanMsg;
        diagnostics_.push_back(diag);
    } catch (const std::exception& e) {
        Diagnostic diag;
        diag.range.start = {0, 0};
        diag.range.end = {0, 1};
        diag.severity = DiagnosticSeverity::Error;
        diag.message = e.what();
        diagnostics_.push_back(diag);
    }
}

std::string DocumentAnalyzer::extractIdentifierAt(Position pos, Range* outRange) const {
    std::string line = getLine(pos.line);
    if (pos.character < 0 || pos.character > static_cast<int>(line.length())) return "";

    int cur = pos.character;
    if (cur == static_cast<int>(line.length()) && cur > 0) cur--;
    if (cur >= static_cast<int>(line.length())) return "";

    auto isIdentChar = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };

    if (!isIdentChar(line[cur])) return "";

    int start = cur;
    while (start > 0 && isIdentChar(line[start - 1])) start--;

    int end = cur;
    while (end < static_cast<int>(line.length()) && isIdentChar(line[end])) end++;

    if (outRange) {
        outRange->start = {pos.line, start};
        outRange->end = {pos.line, end};
    }
    return line.substr(start, end - start);
}

std::string DocumentAnalyzer::extractExpressionAt(Position pos) const {
    std::string line = getLine(pos.line);
    if (pos.character < 0 || pos.character > static_cast<int>(line.length())) return "";

    int cur = pos.character;
    if (cur == static_cast<int>(line.length()) && cur > 0) cur--;
    if (cur >= static_cast<int>(line.length())) return "";

    auto isExprChar = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.';
    };

    if (!isExprChar(line[cur])) return "";

    int start = cur;
    while (start > 0 && isExprChar(line[start - 1])) start--;

    int end = cur;
    while (end < static_cast<int>(line.length()) && isExprChar(line[end])) end++;

    return line.substr(start, end - start);
}

std::string DocumentAnalyzer::extractPrefixBefore(Position pos) const {
    std::string line = getLine(pos.line);
    if (pos.character <= 0) return "";
    int end = std::min(pos.character, static_cast<int>(line.length()));
    int start = end;
    while (start > 0 && (std::isalnum(static_cast<unsigned char>(line[start - 1])) || line[start - 1] == '_' || line[start - 1] == '.')) {
        start--;
    }
    return line.substr(start, end - start);
}

const ScopeSymbol* DocumentAnalyzer::findSymbolInScope(const std::string& name, Position pos) const {
    std::vector<std::shared_ptr<Scope>> matchingScopes;
    std::function<void(std::shared_ptr<Scope>)> findEnclosing = [&](std::shared_ptr<Scope> scope) {
        if (scope->containsLine(pos.line)) {
            matchingScopes.push_back(scope);
            for (auto& child : scope->children) {
                findEnclosing(child);
            }
        }
    };
    if (rootScope_) findEnclosing(rootScope_);

    // Search from innermost matching scope out to root
    for (auto it = matchingScopes.rbegin(); it != matchingScopes.rend(); ++it) {
        for (const auto& sym : (*it)->symbols) {
            if (sym.name == name) {
                return &sym;
            }
        }
    }
    return nullptr;
}

std::vector<DocumentSymbol> DocumentAnalyzer::getDocumentSymbols() const {
    return symbols_;
}

std::optional<Hover> DocumentAnalyzer::getHover(Position pos) const {
    // 1. Try full dotted expression (e.g. table.insert, math.sqrt)
    std::string expr = extractExpressionAt(pos);
    if (!expr.empty()) {
        auto it = s_stdLibDocs.find(expr);
        if (it != s_stdLibDocs.end()) {
            Hover hover;
            hover.contents = "```lua\n" + it->second.signature + "\n```\n\n" + it->second.description;
            return hover;
        }
    }

    // 2. Try single identifier
    std::string ident = extractIdentifierAt(pos);
    if (ident.empty()) return std::nullopt;

    // Check stdlib by identifier
    auto itStd = s_stdLibDocs.find(ident);
    if (itStd != s_stdLibDocs.end()) {
        Hover hover;
        hover.contents = "```lua\n" + itStd->second.signature + "\n```\n\n" + itStd->second.description;
        return hover;
    }

    // Check keywords
    auto itKw = s_keywordDocs.find(ident);
    if (itKw != s_keywordDocs.end()) {
        Hover hover;
        hover.contents = "```lua\n" + ident + "\n```\n\n**Lua Keyword**: " + itKw->second;
        return hover;
    }

    // Check local/document symbols
    const ScopeSymbol* sym = findSymbolInScope(ident, pos);
    if (sym) {
        Hover hover;
        std::string md = "```lua\n" + sym->detail + "\n```";
        if (!sym->doc.empty()) {
            md += "\n\n" + sym->doc;
        } else {
            md += "\n\n*Defined on line " + std::to_string(sym->lineDefined + 1) + "*";
        }
        hover.contents = md;
        return hover;
    }

    return std::nullopt;
}

std::optional<Location> DocumentAnalyzer::getDefinition(Position pos) const {
    std::string ident = extractIdentifierAt(pos);
    if (ident.empty()) return std::nullopt;

    const ScopeSymbol* sym = findSymbolInScope(ident, pos);
    if (sym) {
        Location loc;
        loc.uri = uri_;
        loc.range = sym->range;
        return loc;
    }
    return std::nullopt;
}

std::vector<CompletionItem> DocumentAnalyzer::getCompletions(Position pos) const {
    std::vector<CompletionItem> items;
    std::string prefix = extractPrefixBefore(pos);

    // If prefix contains a dot, offer table member completions
    size_t dotPos = prefix.rfind('.');
    if (dotPos != std::string::npos) {
        std::string tbl = prefix.substr(0, dotPos);
        std::string query = prefix.substr(dotPos + 1);

        for (const auto& [name, doc] : s_stdLibDocs) {
            if (name.rfind(tbl + ".", 0) == 0) {
                std::string member = name.substr(tbl.length() + 1);
                if (query.empty() || member.rfind(query, 0) == 0) {
                    CompletionItem item;
                    item.label = member;
                    item.kind = doc.kind;
                    item.detail = doc.signature;
                    item.documentation = doc.description;
                    item.insertText = member;
                    items.push_back(item);
                }
            }
        }
        return items;
    }

    // Otherwise, general completions: keywords, standard library globals, in-scope symbols
    // Keywords
    for (const auto& [kw, desc] : s_keywordDocs) {
        if (prefix.empty() || kw.rfind(prefix, 0) == 0) {
            CompletionItem item;
            item.label = kw;
            item.kind = CompletionItemKind::Keyword;
            item.detail = "keyword";
            item.documentation = desc;
            item.insertText = kw;
            items.push_back(item);
        }
    }

    // Standard Library Globals
    for (const auto& [name, doc] : s_stdLibDocs) {
        if (name.find('.') == std::string::npos) {
            if (prefix.empty() || name.rfind(prefix, 0) == 0) {
                CompletionItem item;
                item.label = name;
                item.kind = doc.kind;
                item.detail = doc.signature;
                item.documentation = doc.description;
                item.insertText = name;
                items.push_back(item);
            }
        }
    }

    // In-scope symbols
    std::vector<std::shared_ptr<Scope>> matchingScopes;
    std::function<void(std::shared_ptr<Scope>)> findEnclosing = [&](std::shared_ptr<Scope> scope) {
        if (scope->containsLine(pos.line)) {
            matchingScopes.push_back(scope);
            for (auto& child : scope->children) findEnclosing(child);
        }
    };
    if (rootScope_) findEnclosing(rootScope_);

    std::map<std::string, bool> seen;
    for (auto it = matchingScopes.rbegin(); it != matchingScopes.rend(); ++it) {
        for (const auto& sym : (*it)->symbols) {
            if (seen.find(sym.name) == seen.end()) {
                seen[sym.name] = true;
                if (prefix.empty() || sym.name.rfind(prefix, 0) == 0) {
                    CompletionItem item;
                    item.label = sym.name;
                    item.kind = (sym.kind == SymbolKind::Function) ? CompletionItemKind::Function : CompletionItemKind::Variable;
                    item.detail = sym.detail;
                    item.documentation = sym.doc;
                    item.insertText = sym.name;
                    items.push_back(item);
                }
            }
        }
    }

    return items;
}

} // namespace lsp
