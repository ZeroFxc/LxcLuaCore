// lua.hpp
// Lua header files for C++
// 'extern "C" not supplied automatically in lua.h and other headers
// because Lua also compiles as C++

extern "C" {
#include "src/core/lua.h"
#include "src/core/lualib.h"
#include "src/core/lauxlib.h"
}
