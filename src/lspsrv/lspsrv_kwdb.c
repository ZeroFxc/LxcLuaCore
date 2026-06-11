/*
** LXCLUA LSP - Keyword Database
** Comprehensive database of all LXCLUA keywords, built-in functions,
** standard library entries, and their documentation/snippets.
*/

#include <stdlib.h>
#include <string.h>
#include "lspsrv.h"

/* ---- LXCLUA Keywords ---- */

/** @brief All LXCLUA reserved keywords */
static LspKeywordEntry lua_keywords[] = {
    /* Control flow */
    {"and",       COMPLETION_KEYWORD, "Keyword",        "Logical AND operator"},
    {"break",     COMPLETION_KEYWORD, "Keyword",        "Exit from innermost loop"},
    {"continue",  COMPLETION_KEYWORD, "Keyword",        "Skip to next loop iteration"},
    {"do",        COMPLETION_KEYWORD, "Keyword",        "Start a new block"},
    {"else",      COMPLETION_KEYWORD, "Keyword",        "Alternative branch in if statement"},
    {"elseif",    COMPLETION_KEYWORD, "Keyword",        "Additional condition in if statement"},
    {"end",       COMPLETION_KEYWORD, "Keyword",        "End a block"},
    {"false",     COMPLETION_KEYWORD, "Keyword",        "Boolean false value"},
    {"for",       COMPLETION_KEYWORD, "Keyword",        "For loop (numeric or generic)"},
    {"function",  COMPLETION_KEYWORD, "Keyword",        "Define a function"},
    {"goto",      COMPLETION_KEYWORD, "Keyword",        "Jump to a label"},
    {"if",        COMPLETION_KEYWORD, "Keyword",        "Conditional statement"},
    {"in",        COMPLETION_KEYWORD, "Keyword",        "Iteration specifier in for loop"},
    {"local",     COMPLETION_KEYWORD, "Keyword",        "Declare a local variable"},
    {"nil",       COMPLETION_KEYWORD, "Keyword",        "Nil value (null)"},
    {"not",       COMPLETION_KEYWORD, "Keyword",        "Logical NOT operator"},
    {"or",        COMPLETION_KEYWORD, "Keyword",        "Logical OR operator"},
    {"repeat",    COMPLETION_KEYWORD, "Keyword",        "Repeat-until loop"},
    {"return",    COMPLETION_KEYWORD, "Keyword",        "Return from a function"},
    {"then",      COMPLETION_KEYWORD, "Keyword",        "Then clause in if statement"},
    {"true",      COMPLETION_KEYWORD, "Keyword",        "Boolean true value"},
    {"until",     COMPLETION_KEYWORD, "Keyword",        "End condition of repeat loop"},
    {"while",     COMPLETION_KEYWORD, "Keyword",        "While loop"},

    /* LXCLUA modern keywords */
    {"async",     COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Declare an async function"},
    {"await",     COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Await an async operation result"},
    {"bool",      COMPLETION_KEYWORD, "Type (LXCLUA)",    "Boolean type annotation"},
    {"case",      COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Case clause in switch statement"},
    {"catch",     COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Catch clause in try statement"},
    {"char",      COMPLETION_KEYWORD, "Type (LXCLUA)",    "Character type annotation"},
    {"class",     COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Define a class"},
    {"const",     COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Declare a constant variable"},
    {"default",   COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Default clause in switch statement"},
    {"defer",     COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Defer execution to end of scope"},
    {"double",    COMPLETION_KEYWORD, "Type (LXCLUA)",    "Double precision float type"},
    {"enum",      COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Define an enumeration"},
    {"export",    COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Export a symbol from module"},
    {"finally",   COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Finally clause in try statement"},
    {"float",     COMPLETION_KEYWORD, "Type (LXCLUA)",    "Float type annotation"},
    {"global",    COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Declare a global variable"},
    {"instanceof",COMPLETION_OPERATOR, "Operator (LXCLUA)", "Check object instance of class"},
    {"int",       COMPLETION_KEYWORD, "Type (LXCLUA)",    "Integer type annotation"},
    {"is",        COMPLETION_OPERATOR, "Operator (LXCLUA)", "Type check operator"},
    {"lambda",    COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Create a lambda/anonymous function"},
    {"let",       COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Declare a block-scoped variable"},
    {"long",      COMPLETION_KEYWORD, "Type (LXCLUA)",    "Long integer type annotation"},
    {"namespace", COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Define a namespace"},
    {"requires",  COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Module dependency declaration"},
    {"struct",    COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Define a struct type"},
    {"superstruct",COMPLETION_KEYWORD,"Keyword (LXCLUA)", "Define an extended struct type"},
    {"switch",    COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Switch statement"},
    {"take",      COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Ownership transfer operator"},
    {"try",       COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Try-catch-finally statement"},
    {"using",     COMPLETION_KEYWORD, "Keyword (LXCLUA)", "RAII-style resource management"},
    {"void",      COMPLETION_KEYWORD, "Type (LXCLUA)",    "Void type annotation"},
    {"when",      COMPLETION_KEYWORD, "Keyword (LXCLUA)", "Pattern guard condition"},
    {"with",      COMPLETION_KEYWORD, "Keyword (LXCLUA)", "With-resource statement"},
    {NULL, 0, NULL, NULL}
};

/* ---- LXCLUA Built-in Functions (global scope) ---- */

static LspKeywordEntry lua_builtins[] = {
    /* Core functions */
    {"assert",     COMPLETION_FUNCTION, "function(condition, message?)", "Assert a condition is true, raises error if false"},
    {"collectgarbage",COMPLETION_FUNCTION,"function(opt?, arg?)", "Control garbage collector"},
    {"dofile",     COMPLETION_FUNCTION, "function(filename)", "Execute Lua file, returns all results"},
    {"error",      COMPLETION_FUNCTION, "function(message, level?)", "Raises an error with message"},
    {"getmetatable",COMPLETION_FUNCTION,"function(object)", "Get object's metatable"},
    {"ipairs",     COMPLETION_FUNCTION, "function(t)", "Iterator for indexed table pairs"},
    {"load",       COMPLETION_FUNCTION, "function(chunk, chunkname?, mode?, env?)", "Load a Lua chunk"},
    {"loadfile",   COMPLETION_FUNCTION, "function(filename, mode?, env?)", "Load a Lua file as a chunk"},
    {"next",       COMPLETION_FUNCTION, "function(table, index?)", "Get next key-value pair from table"},
    {"pairs",      COMPLETION_FUNCTION, "function(t)", "Iterator for all table key-value pairs"},
    {"pcall",      COMPLETION_FUNCTION, "function(f, arg1?, ...)", "Protected call, catches errors"},
    {"print",      COMPLETION_FUNCTION, "function(...)", "Print values to stdout"},
    {"rawequal",   COMPLETION_FUNCTION, "function(v1, v2)", "Compare values without metamethods"},
    {"rawget",     COMPLETION_FUNCTION, "function(table, index)", "Get table value without __index"},
    {"rawlen",     COMPLETION_FUNCTION, "function(v)", "Get raw length of a value"},
    {"rawset",     COMPLETION_FUNCTION, "function(table, index, value)", "Set table value without __newindex"},
    {"require",    COMPLETION_FUNCTION, "function(modname)", "Load and run a module"},
    {"select",     COMPLETION_FUNCTION, "function(index, ...)", "Select arguments by index or count"},
    {"setmetatable",COMPLETION_FUNCTION,"function(table, metatable)", "Set table's metatable"},
    {"tonumber",   COMPLETION_FUNCTION, "function(e, base?)", "Convert value to number"},
    {"tostring",   COMPLETION_FUNCTION, "function(e)", "Convert value to string"},
    {"type",       COMPLETION_FUNCTION, "function(v)", "Get the type name of a value"},
    {"_VERSION",   COMPLETION_CONSTANT,  "string", "Lua version string"},
    {"xpcall",     COMPLETION_FUNCTION, "function(f, msgh, arg1?, ...)", "Protected call with custom error handler"},
    {"warn",       COMPLETION_FUNCTION, "function(msg1, ...)", "Emit a warning message"},

    /* LXCLUA extended builtins */
    {"js_import",  COMPLETION_FUNCTION, "function(name)", "LXCLUA: Import JS module (WASM)"},
    {"__FILE__",   COMPLETION_CONSTANT,  "string", "LXCLUA: Current source file path"},
    {"__LINE__",   COMPLETION_CONSTANT,  "number", "LXCLUA: Current source line number"},
    {"__FUNCTION__",COMPLETION_CONSTANT, "string", "LXCLUA: Current function name"},
    {"import",     COMPLETION_FUNCTION, "function(name)", "LXCLUA: Import a module"},
    {"module",     COMPLETION_FUNCTION, "function(name, ...)", "LXCLUA: Create a module"},

    {NULL, 0, NULL, NULL}
};

/* ---- LXCLUA Standard Library Functions ---- */

static LspKeywordEntry lua_stdlib[] = {
    /* string library */
    {"string.byte",      COMPLETION_FUNCTION, "function(s, i?, j?)", "Get internal numeric codes of characters"},
    {"string.char",      COMPLETION_FUNCTION, "function(...)", "Create string from numeric codes"},
    {"string.dump",      COMPLETION_FUNCTION, "function(function, strip?)", "Dump function as binary chunk"},
    {"string.find",      COMPLETION_FUNCTION, "function(s, pattern, init?, plain?)", "Find pattern in string"},
    {"string.format",    COMPLETION_FUNCTION, "function(fmt, ...)", "Format string (like sprintf)"},
    {"string.gmatch",    COMPLETION_FUNCTION, "function(s, pattern)", "Iterator over pattern matches"},
    {"string.gsub",      COMPLETION_FUNCTION, "function(s, pattern, repl, n?)", "Global string substitution"},
    {"string.len",       COMPLETION_FUNCTION, "function(s)", "Get string length"},
    {"string.lower",     COMPLETION_FUNCTION, "function(s)", "Convert to lowercase"},
    {"string.match",     COMPLETION_FUNCTION, "function(s, pattern, init?)", "Find and capture pattern match"},
    {"string.pack",      COMPLETION_FUNCTION, "function(fmt, v1, ...)", "Pack values into binary string"},
    {"string.packsize",  COMPLETION_FUNCTION, "function(fmt)", "Get packed size of format string"},
    {"string.rep",       COMPLETION_FUNCTION, "function(s, n, sep?)", "Repeat string n times"},
    {"string.reverse",   COMPLETION_FUNCTION, "function(s)", "Reverse string"},
    {"string.sub",       COMPLETION_FUNCTION, "function(s, i, j?)", "Get substring"},
    {"string.unpack",    COMPLETION_FUNCTION, "function(fmt, s, pos?)", "Unpack binary string"},
    {"string.upper",     COMPLETION_FUNCTION, "function(s)", "Convert to uppercase"},

    /* table library */
    {"table.concat",     COMPLETION_FUNCTION, "function(list, sep?, i?, j?)", "Concatenate table elements"},
    {"table.insert",     COMPLETION_FUNCTION, "function(list, pos, value?)", "Insert value into table"},
    {"table.move",       COMPLETION_FUNCTION, "function(a1, f, e, t, a2?)", "Move table elements"},
    {"table.pack",       COMPLETION_FUNCTION, "function(...)", "Pack arguments into a table"},
    {"table.remove",     COMPLETION_FUNCTION, "function(list, pos?)", "Remove element from table"},
    {"table.sort",       COMPLETION_FUNCTION, "function(list, comp?)", "Sort table elements"},
    {"table.unpack",     COMPLETION_FUNCTION, "function(list, i?, j?)", "Unpack table into values"},

    /* math library */
    {"math.abs",         COMPLETION_FUNCTION, "function(x)", "Absolute value"},
    {"math.acos",        COMPLETION_FUNCTION, "function(x)", "Arc cosine (radians)"},
    {"math.asin",        COMPLETION_FUNCTION, "function(x)", "Arc sine (radians)"},
    {"math.atan",        COMPLETION_FUNCTION, "function(y, x?)", "Arc tangent (radians)"},
    {"math.ceil",        COMPLETION_FUNCTION, "function(x)", "Ceiling (round up)"},
    {"math.cos",         COMPLETION_FUNCTION, "function(x)", "Cosine (x in radians)"},
    {"math.deg",         COMPLETION_FUNCTION, "function(x)", "Radians to degrees"},
    {"math.exp",         COMPLETION_FUNCTION, "function(x)", "e^x"},
    {"math.floor",       COMPLETION_FUNCTION, "function(x)", "Floor (round down)"},
    {"math.fmod",        COMPLETION_FUNCTION, "function(x, y)", "Floating point remainder"},
    {"math.huge",        COMPLETION_CONSTANT,  "number", "Infinity constant"},
    {"math.log",         COMPLETION_FUNCTION, "function(x, base?)", "Natural logarithm"},
    {"math.max",         COMPLETION_FUNCTION, "function(x, ...)", "Maximum value"},
    {"math.maxinteger",  COMPLETION_CONSTANT,  "integer", "Maximum representable integer"},
    {"math.min",         COMPLETION_FUNCTION, "function(x, ...)", "Minimum value"},
    {"math.mininteger",  COMPLETION_CONSTANT,  "integer", "Minimum representable integer"},
    {"math.modf",        COMPLETION_FUNCTION, "function(x)", "Split into integer and fractional parts"},
    {"math.pi",          COMPLETION_CONSTANT,  "number", "Pi constant (3.14159...)"},
    {"math.rad",         COMPLETION_FUNCTION, "function(x)", "Degrees to radians"},
    {"math.random",      COMPLETION_FUNCTION, "function(m?, n?)", "Generate random number"},
    {"math.randomseed",  COMPLETION_FUNCTION, "function(x)", "Set random seed"},
    {"math.sin",         COMPLETION_FUNCTION, "function(x)", "Sine (x in radians)"},
    {"math.sqrt",        COMPLETION_FUNCTION, "function(x)", "Square root"},
    {"math.tan",         COMPLETION_FUNCTION, "function(x)", "Tangent (x in radians)"},
    {"math.tointeger",   COMPLETION_FUNCTION, "function(x)", "Convert to integer if possible"},
    {"math.type",        COMPLETION_FUNCTION, "function(x)", "Subtype of number (integer/float)"},
    {"math.ult",         COMPLETION_FUNCTION, "function(m, n)", "Unsigned less-than comparison"},

    /* io library */
    {"io.close",         COMPLETION_FUNCTION, "function(file?)", "Close a file"},
    {"io.flush",         COMPLETION_FUNCTION, "function()", "Flush output"},
    {"io.input",         COMPLETION_FUNCTION, "function(file?)", "Set default input file"},
    {"io.lines",         COMPLETION_FUNCTION, "function(filename?, ...)", "Iterator over file lines"},
    {"io.open",          COMPLETION_FUNCTION, "function(filename, mode?)", "Open a file"},
    {"io.output",        COMPLETION_FUNCTION, "function(file?)", "Set default output file"},
    {"io.popen",         COMPLETION_FUNCTION, "function(prog, mode?)", "Open process pipe"},
    {"io.read",          COMPLETION_FUNCTION, "function(...)", "Read from default input file"},
    {"io.tmpfile",       COMPLETION_FUNCTION, "function()", "Open temporary file"},
    {"io.type",          COMPLETION_FUNCTION, "function(obj)", "Check if obj is open file handle"},
    {"io.write",         COMPLETION_FUNCTION, "function(...)", "Write to default output file"},

    /* os library */
    {"os.clock",         COMPLETION_FUNCTION, "function()", "Approximate CPU time"},
    {"os.date",          COMPLETION_FUNCTION, "function(format?, time?)", "Get formatted date/time"},
    {"os.difftime",      COMPLETION_FUNCTION, "function(t2, t1)", "Time difference in seconds"},
    {"os.execute",       COMPLETION_FUNCTION, "function(command?)", "Execute shell command"},
    {"os.exit",          COMPLETION_FUNCTION, "function(code?, close?)", "Exit program"},
    {"os.getenv",        COMPLETION_FUNCTION, "function(varname)", "Get environment variable"},
    {"os.remove",        COMPLETION_FUNCTION, "function(filename)", "Delete file"},
    {"os.rename",        COMPLETION_FUNCTION, "function(oldname, newname)", "Rename file"},
    {"os.setlocale",     COMPLETION_FUNCTION, "function(locale, category?)", "Set locale"},
    {"os.time",          COMPLETION_FUNCTION, "function(table?)", "Get or compute timestamp"},
    {"os.tmpname",       COMPLETION_FUNCTION, "function()", "Get temp file name"},

    /* coroutine library */
    {"coroutine.create",     COMPLETION_FUNCTION, "function(f)", "Create a coroutine"},
    {"coroutine.isyieldable",COMPLETION_FUNCTION,"function()", "Check if running coroutine can yield"},
    {"coroutine.resume",     COMPLETION_FUNCTION, "function(co, ...)", "Resume a coroutine"},
    {"coroutine.running",    COMPLETION_FUNCTION, "function()", "Get running coroutine"},
    {"coroutine.status",     COMPLETION_FUNCTION, "function(co)", "Get coroutine status"},
    {"coroutine.wrap",       COMPLETION_FUNCTION, "function(f)", "Create wrapper function for coroutine"},
    {"coroutine.yield",      COMPLETION_FUNCTION, "function(...)", "Yield from coroutine"},

    /* debug library */
    {"debug.debug",          COMPLETION_FUNCTION, "function()", "Enter interactive debug mode"},
    {"debug.gethook",        COMPLETION_FUNCTION, "function(thread?)", "Get current hook settings"},
    {"debug.getinfo",        COMPLETION_FUNCTION, "function(f, what?)", "Get function/chunk info"},
    {"debug.getlocal",       COMPLETION_FUNCTION, "function(f, index)", "Get local variable value"},
    {"debug.getmetatable",   COMPLETION_FUNCTION, "function(value)", "Get metatable of value"},
    {"debug.getregistry",    COMPLETION_FUNCTION, "function()", "Get the registry table"},
    {"debug.getupvalue",     COMPLETION_FUNCTION, "function(f, up)", "Get upvalue of function"},
    {"debug.getuservalue",   COMPLETION_FUNCTION, "function(u)", "Get userdata user value"},
    {"debug.sethook",        COMPLETION_FUNCTION, "function(hook, mask, count?)", "Set debug hook"},
    {"debug.setlocal",       COMPLETION_FUNCTION, "function(level, index, value)", "Set local value"},
    {"debug.setmetatable",   COMPLETION_FUNCTION, "function(value, table)", "Set metatable"},
    {"debug.setupvalue",     COMPLETION_FUNCTION, "function(f, up, value)", "Set function upvalue"},
    {"debug.setuservalue",   COMPLETION_FUNCTION, "function(u, value)", "Set userdata user value"},
    {"debug.traceback",      COMPLETION_FUNCTION, "function(message?, level?)", "Get stack traceback"},
    {"debug.upvalueid",      COMPLETION_FUNCTION, "function(f, n)", "Get unique ID of upvalue"},
    {"debug.upvaluejoin",    COMPLETION_FUNCTION, "function(f1, n1, f2, n2)", "Make upvalues share value"},

    /* utf8 library */
    {"utf8.char",            COMPLETION_FUNCTION, "function(...)", "Create UTF-8 string from code points"},
    {"utf8.charpattern",     COMPLETION_CONSTANT,"string", "Pattern to match one UTF-8 byte sequence"},
    {"utf8.codes",           COMPLETION_FUNCTION, "function(s)", "Iterator over code points"},
    {"utf8.codepoint",       COMPLETION_FUNCTION, "function(s, i?, j?)", "Get code points of characters"},
    {"utf8.len",             COMPLETION_FUNCTION, "function(s, i?, j?)", "Get length in code points"},
    {"utf8.offset",          COMPLETION_FUNCTION, "function(s, n, i?)", "Get byte offset from code point index"},

    /* LXCLUA crypto library */
    {"crypto.sha1",         COMPLETION_FUNCTION, "function(data)", "LXCLUA: Compute SHA-1 hash"},
    {"crypto.sha256",       COMPLETION_FUNCTION, "function(data)", "LXCLUA: Compute SHA-256 hash"},
    {"crypto.aes_encrypt",  COMPLETION_FUNCTION, "function(key, data)", "LXCLUA: AES encryption"},
    {"crypto.aes_decrypt",  COMPLETION_FUNCTION, "function(key, data)", "LXCLUA: AES decryption"},
    {"crypto.crc32",        COMPLETION_FUNCTION, "function(data)", "LXCLUA: Compute CRC32 checksum"},
    {"crypto.random",       COMPLETION_FUNCTION, "function(len)", "LXCLUA: Generate random bytes"},
    {"crypto.rsa_encrypt",  COMPLETION_FUNCTION, "function(key, data)", "LXCLUA: RSA encryption"},
    {"crypto.rsa_decrypt",  COMPLETION_FUNCTION, "function(key, data)", "LXCLUA: RSA decryption"},
    {"crypto.rsa_sign",     COMPLETION_FUNCTION, "function(key, data)", "LXCLUA: RSA signature"},
    {"crypto.rsa_verify",   COMPLETION_FUNCTION, "function(key, data, sig)", "LXCLUA: RSA verification"},
    {"crypto.ecc_sign",     COMPLETION_FUNCTION, "function(key, data)", "LXCLUA: ECC signature"},
    {"crypto.ecc_verify",   COMPLETION_FUNCTION, "function(key, data, sig)", "LXCLUA: ECC verification"},
    {"crypto.ecc_ecdh",     COMPLETION_FUNCTION, "function(priv, peer_pub)", "LXCLUA: ECDH key exchange"},
    {"crypto.uuid",         COMPLETION_FUNCTION, "function()", "LXCLUA: Generate UUID v4"},

    /* LXCLUA async/promise */
    {"promise.new",       COMPLETION_FUNCTION, "function(f)", "LXCLUA: Create a new Promise"},
    {"promise.resolve",   COMPLETION_FUNCTION, "function(value)", "LXCLUA: Create resolved Promise"},
    {"promise.reject",    COMPLETION_FUNCTION, "function(reason)", "LXCLUA: Create rejected Promise"},
    {"promise.all",       COMPLETION_FUNCTION, "function(promises)", "LXCLUA: Wait for all promises"},
    {"promise.race",      COMPLETION_FUNCTION, "function(promises)", "LXCLUA: Wait for first settled"},

    /* LXCLUA HTTP */
    {"http.get",          COMPLETION_FUNCTION, "function(url, headers?)", "LXCLUA: HTTP GET request"},
    {"http.post",         COMPLETION_FUNCTION, "function(url, data, headers?)", "LXCLUA: HTTP POST request"},
    {"http.createServer", COMPLETION_FUNCTION, "function(host, port)", "LXCLUA: Create HTTP server"},

    /* LXCLUA threading */
    {"thread.create",     COMPLETION_FUNCTION, "function(f, ...)", "LXCLUA: Create a new thread"},
    {"thread.join",       COMPLETION_FUNCTION, "function(thread)", "LXCLUA: Wait for thread to finish"},
    {"thread.detach",     COMPLETION_FUNCTION, "function(thread)", "LXCLUA: Detach thread"},

    /* LXCLUA wasm3 */
    {"wasm3.newEnvironment",    COMPLETION_FUNCTION, "function()", "LXCLUA: Create WASM3 environment"},
    {"wasm3.newRuntime",        COMPLETION_FUNCTION, "function(env)", "LXCLUA: Create WASM3 runtime"},

    /* LXCLUA wasmtime */
    {"wasmtime.newEngine",      COMPLETION_FUNCTION, "function()", "LXCLUA: Create Wasmtime engine"},
    {"wasmtime.newStore",       COMPLETION_FUNCTION, "function(engine)", "LXCLUA: Create Wasmtime store"},
    {"wasmtime.newModule",      COMPLETION_FUNCTION, "function(engine, wasm_bytes)", "LXCLUA: Compile WASM module"},
    {"wasmtime.newInstance",    COMPLETION_FUNCTION, "function(store, module, imports?)", "LXCLUA: Instantiate WASM module"},

    /* LXCLUA bit */
    {"bit32.arshift",    COMPLETION_FUNCTION, "function(x, n)", "Arithmetic right shift"},
    {"bit32.band",       COMPLETION_FUNCTION, "function(...)", "Bitwise AND"},
    {"bit32.bnot",       COMPLETION_FUNCTION, "function(x)", "Bitwise NOT"},
    {"bit32.bor",        COMPLETION_FUNCTION, "function(...)", "Bitwise OR"},
    {"bit32.btest",      COMPLETION_FUNCTION, "function(...)", "Test bits"},
    {"bit32.bxor",       COMPLETION_FUNCTION, "function(...)", "Bitwise XOR"},
    {"bit32.extract",    COMPLETION_FUNCTION, "function(n, field, width?)", "Extract bit field"},
    {"bit32.replace",    COMPLETION_FUNCTION, "function(n, v, field, width?)", "Replace bit field"},
    {"bit32.lrotate",    COMPLETION_FUNCTION, "function(x, n)", "Rotate left"},
    {"bit32.lshift",     COMPLETION_FUNCTION, "function(x, n)", "Logical left shift"},
    {"bit32.rrotate",    COMPLETION_FUNCTION, "function(x, n)", "Rotate right"},
    {"bit32.rshift",     COMPLETION_FUNCTION, "function(x, n)", "Logical right shift"},

    {NULL, 0, NULL, NULL}
};

/* ---- LXCLUA Code Snippets ---- */

/** @brief Common LXCLUA code snippets */
static LspKeywordEntry lua_snippets[] = {
    {"function", COMPLETION_SNIPPET, "Snippet", "Function definition",
     "function ${1:name}(${2:params})\n\t${0:body}\nend"},
    {"local function", COMPLETION_SNIPPET, "Snippet", "Local function definition",
     "local function ${1:name}(${2:params})\n\t${0:body}\nend"},
    {"if", COMPLETION_SNIPPET, "Snippet", "If statement",
     "if ${1:condition} then\n\t${0:body}\nend"},
    {"ifelse", COMPLETION_SNIPPET, "Snippet", "If-else statement",
     "if ${1:condition} then\n\t${2:body}\nelse\n\t${0:else_body}\nend"},
    {"ifeif", COMPLETION_SNIPPET, "Snippet", "If-elseif-else statement",
     "if ${1:cond1} then\n\t${2:body1}\nelseif ${3:cond2} then\n\t${4:body2}\nelse\n\t${0:else_body}\nend"},
    {"fori", COMPLETION_SNIPPET, "Snippet", "Numeric for loop",
     "for ${1:i} = ${2:1}, ${3:10} do\n\t${0:body}\nend"},
    {"forp", COMPLETION_SNIPPET, "Snippet", "Generic for loop (pairs)",
     "for ${1:k}, ${2:v} in pairs(${3:table}) do\n\t${0:body}\nend"},
    {"fori_pairs", COMPLETION_SNIPPET, "Snippet", "Generic for loop (ipairs)",
     "for ${1:i}, ${2:v} in ipairs(${3:table}) do\n\t${0:body}\nend"},
    {"while", COMPLETION_SNIPPET, "Snippet", "While loop",
     "while ${1:condition} do\n\t${0:body}\nend"},
    {"repeat", COMPLETION_SNIPPET, "Snippet", "Repeat-until loop",
     "repeat\n\t${0:body}\nuntil ${1:condition}"},
    {"class", COMPLETION_SNIPPET, "Snippet", "LXCLUA class definition",
     "class ${1:ClassName}\n\t${2:body}\nend"},
    {"class inherit", COMPLETION_SNIPPET, "Snippet", "LXCLUA class with inheritance",
     "class ${1:ChildClass} : ${2:ParentClass}\n\t${3:body}\nend"},
    {"struct", COMPLETION_SNIPPET, "Snippet", "LXCLUA struct definition",
     "struct ${1:StructName}\n\t${2:fields}\nend"},
    {"namespace", COMPLETION_SNIPPET, "Snippet", "LXCLUA namespace definition",
     "namespace ${1:Name}\n\t${0:body}\nend"},
    {"enum", COMPLETION_SNIPPET, "Snippet", "LXCLUA enum definition",
     "enum ${1:EnumName}\n\t${2:members}\nend"},
    {"switch", COMPLETION_SNIPPET, "Snippet", "LXCLUA switch statement",
     "switch ${1:expr}\n\tcase ${2:value1}:\n\t\t${3:body1}\n\tcase ${4:value2}:\n\t\t${5:body2}\n\tdefault:\n\t\t${0:default_body}\nend"},
    {"try", COMPLETION_SNIPPET, "Snippet", "LXCLUA try-catch statement",
     "try\n\t${1:body}\ncatch (${2:e})\n\t${3:handler}\nend"},
    {"try finally", COMPLETION_SNIPPET, "Snippet", "LXCLUA try-catch-finally",
     "try\n\t${1:body}\ncatch (${2:e})\n\t${3:handler}\nfinally\n\t${0:cleanup}\nend"},
    {"async function", COMPLETION_SNIPPET, "Snippet", "LXCLUA async function",
     "async function ${1:name}(${2:params})\n\t${3:body}\n\tawait ${0:expr}\nend"},
    {"log", COMPLETION_SNIPPET, "Snippet", "Print with formatting",
     "print(string.format(\"${1:format}\", ${0:args}))"},
    {"require", COMPLETION_SNIPPET, "Snippet", "Module require",
     "local ${1:mod} = require(\"${1:mod}\")"},
    {NULL, 0, NULL, NULL}
};

/*
 * @brief 获取LXCLUA关键字列表
 * @param out 输出-关键字数组指针
 * @return 关键字数量
 */
int lsp_kwdb_get_keywords(LspKeywordEntry **out) {
    *out = lua_keywords;
    int count = 0;
    while (lua_keywords[count].name) count++;
    return count;
}

/*
 * @brief 获取内置函数列表
 * @param out 输出-内置函数数组指针
 * @return 数量
 */
int lsp_kwdb_get_builtins(LspKeywordEntry **out) {
    *out = lua_builtins;
    int count = 0;
    while (lua_builtins[count].name) count++;
    return count;
}

/*
 * @brief 获取标准库函数列表
 * @param out 输出-函数数组指针
 * @return 数量
 */
int lsp_kwdb_get_stdlib(LspKeywordEntry **out) {
    *out = lua_stdlib;
    int count = 0;
    while (lua_stdlib[count].name) count++;
    return count;
}

/*
 * @brief 根据名称查找文档说明
 * @param name 函数或关键字名
 * @return 文档字符串，NULL表示未找到
 */
const char *lsp_kwdb_find_doc(const char *name) {
    if (!name) return NULL;
    /* Search keywords */
    for (int i = 0; lua_keywords[i].name; i++)
        if (strcmp(lua_keywords[i].name, name) == 0)
            return lua_keywords[i].documentation;
    /* Search builtins */
    for (int i = 0; lua_builtins[i].name; i++)
        if (strcmp(lua_builtins[i].name, name) == 0)
            return lua_builtins[i].documentation;
    /* Search stdlib */
    for (int i = 0; lua_stdlib[i].name; i++)
        if (strcmp(lua_stdlib[i].name, name) == 0)
            return lua_stdlib[i].documentation;
    return NULL;
}

/*
 * @brief 获取所有LXCLUA代码片段
 * @param out 输出-片段数组指针
 * @return 片段数量
 */
int lsp_kwdb_get_snippets(LspKeywordEntry **out) {
    *out = lua_snippets;
    int count = 0;
    while (lua_snippets[count].name) count++;
    return count;
}