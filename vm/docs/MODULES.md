# Modular Development Plan

## 1. Directory Structure

```text
.
├── docs/
│   ├── DESIGN.md
│   └── MODULES.md
├── translator/
│   ├── Makefile          # Build system for the C Translator
│   ├── main.c            # Entry point of the translator
│   ├── translate.c       # Core translation logic (Lua 5.1 -> Custom IR)
│   ├── translate.h
│   ├── dump.c            # Serializes the custom IR to a Lua-readable text/JSON format
│   └── dump.h
└── vm/
    ├── main.lua          # Entry point for the Lua VM
    ├── loader.lua        # Deserializes the translated file into memory
    ├── state.lua         # Manages the execution context (stack, registers, frames)
    ├── core.lua          # Core execution loop and instruction dispatch
    └── opcodes.lua       # Defines custom opcode behaviors
```

## 2. Modules Breakdown: C Translator (`translator/`)

### 2.1 `main.c`
- Responsible for parsing command-line arguments (e.g., input `.lua` file, output translated file).
- Initializes a Lua 5.1 state `luaL_newstate()`.
- Uses `luaL_loadfile()` to parse the input file into a Proto (function prototype).
- Calls the translation and dumping modules.

### 2.2 `translate.c` & `translate.h`
- Interfaces directly with Lua 5.1 internals (requires compiling against Lua 5.1 headers or embedding them).
- Takes a `Proto*` (from Lua's internal `lobject.h`) and recursively converts it into an intermediate representation (IR) suitable for our custom VM.
- Maps `OP_*` standard opcodes to `VOP_*` custom string/integer identifiers.

### 2.3 `dump.c` & `dump.h`
- Takes the translated IR and serializes it.
- To avoid writing a complex binary parser in pure Lua 5.1 (which lacks `string.unpack`), this module will output the VM instructions and constants as a plain Lua script that returns a table representing the prototype.
  - E.g., it writes `return { numparams = 0, code = { {op="VOP_MOV", a=0, b=1}, ... } }` to the output file.

## 3. Modules Breakdown: Pure Lua VM (`vm/`)

### 3.1 `vm/main.lua`
- Entry point. Reads the output file from the translator.
- Initializes the `state.lua`.
- Passes the loaded prototype to `core.lua` to begin execution.

### 3.2 `vm/loader.lua`
- Since the translator outputs a valid Lua file (that returns a table), the loader can simply use `dofile()` or `loadfile()` to parse the "bytecode" into a Lua table structure.
- Builds any necessary nested prototype structures.

### 3.3 `vm/state.lua`
- Defines the `LuaState` object.
- **Fields:**
  - `stack`: A table representing the virtual register stack.
  - `top`: Integer representing the top of the stack.
  - `call_frames`: A stack of call frames.
  - `globals`: The global environment table for the virtual machine (can be linked to standard Lua `_G` or isolated).
- Provides methods for pushing/popping frames, resolving upvalues, and manipulating the stack.

### 3.4 `vm/opcodes.lua`
- Contains a dictionary/array of functions corresponding to each `VOP_*` instruction.
- Each function takes the `state` and the instruction data (`inst`) and mutates the state accordingly.
- *Example:*
  ```lua
  local opcodes = {}
  function opcodes.VOP_MOV(state, inst)
      state.stack[state.base + inst.a] = state.stack[state.base + inst.b]
  end
  return opcodes
  ```

### 3.5 `vm/core.lua`
- Contains the main dispatch loop.
- Takes a `LuaState`.
- Loops continuously: fetches the next instruction from the current call frame's `pc`, looks up the function in `opcodes.lua`, executes it, and increments `pc`.

## 4. Implementation Strategy

### Phase 1: Skeleton and Build System
- Create the folder structure.
- Write a simple `Makefile` for the translator (stubbing out the translation logic).
- Create empty stub files for the Lua modules.

### Phase 2: Translation Output (C Side)
- Include Lua 5.1 source files/headers in the `translator/` directory.
- Write `main.c` to parse a script.
- Write `dump.c` to iterate over the `Proto`'s instructions, constants, and nested protos, outputting them as formatted Lua text.

### Phase 3: Basic VM Execution (Lua Side)
- Write `loader.lua` to read the dumped format.
- Write `state.lua` to initialize the stack.
- Write `core.lua` to loop through instructions.
- Implement a single instruction (e.g., `LOADK` and `RETURN`) to verify end-to-end functionality.

### Phase 4: Full Instruction Set
- Iteratively implement the remaining Lua 5.1 opcodes in `translate.c` and their matching logic in `vm/opcodes.lua`.
- Test arithmetic, loops, conditionals, function calls, and closures.