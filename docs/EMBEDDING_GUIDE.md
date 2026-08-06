# LXCLUA-NCore 嵌入指南

> 本文档面向需要在 C/C++ 应用程序中嵌入 LXCLUA-NCore 运行时的开发者。

---

## 目录

- [1. 快速开始](#1-快速开始)
- [2. 编译与链接](#2-编译与链接)
- [3. 基础嵌入模式](#3-基础嵌入模式)
- [4. 暴露 C 函数给 Lua](#4-暴露-c-函数给-lua)
- [5. 暴露 C++ 类给 Lua](#5-暴露-c-类给-lua)
- [6. 错误处理](#6-错误处理)
- [7. 内存管理](#7-内存管理)
- [8. 安全性最佳实践](#8-安全性最佳实践)
- [9. 完整示例](#9-完整示例)

---

## 1. 快速开始

### 1.1 最小嵌入示例

```c
#include "lxclua.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

int main(void) {
    /* 1. 创建 Lua 状态 */
    lua_State *L = luaL_newstate();
    if (!L) {
        fprintf(stderr, "Failed to create Lua state\n");
        return 1;
    }

    /* 2. 打开标准库 */
    luaL_openlibs(L);

    /* 3. 执行 Lua 代码 */
    if (luaL_dostring(L, "print('Hello from LXCLUA!')") != LUA_OK) {
        fprintf(stderr, "Error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    /* 4. 销毁状态 */
    lua_close(L);
    return 0;
}
```

### 1.2 编译命令

```bash
# Linux
gcc -o myapp main.c -llxclua -lm -ldl -I/path/to/lxclua/include

# macOS
clang -o myapp main.c -llxclua -lm -I/path/to/lxclua/include

# Windows (MinGW)
gcc -o myapp.exe main.c -llxclua -lm -I/path/to/lxclua/include
```

---

## 2. 编译与链接

### 2.1 使用静态库

```bash
# 构建 LXCLUA 静态库
cd lxclua-core
make lib

# 链接到项目
gcc -o myapp main.c liblxclua.a -lm -ldl -lpthread -I./src
```

### 2.2 使用 CMake

```cmake
cmake_minimum_required(VERSION 3.15)
project(myapp C)

set(CMAKE_C_STANDARD 23)

# LXCLUA 作为子目录
add_subdirectory(lxclua-core)

add_executable(myapp main.c)
target_link_libraries(myapp PRIVATE lxclua)
target_include_directories(myapp PRIVATE lxclua-core/src)
```

### 2.3 使用 pkg-config

```bash
pkg-config --cflags lxclua
pkg-config --libs lxclua
```

---

## 3. 基础嵌入模式

### 3.1 执行 Lua 文件

```c
/* 执行单个 Lua 文件 */
int run_lua_file(lua_State *L, const char *filename) {
    int err = luaL_dofile(L, filename);
    if (err != LUA_OK) {
        fprintf(stderr, "Error in %s: %s\n", filename, lua_tostring(L, -1));
        lua_pop(L, 1); /* 弹出错误消息 */
        return err;
    }
    return LUA_OK;
}
```

### 3.2 执行 Lua 字符串

```c
/* 执行 Lua 代码字符串 */
int run_lua_string(lua_State *L, const char *code) {
    int err = luaL_dostring(L, code);
    if (err != LUA_OK) {
        fprintf(stderr, "Error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return err;
    }
    return LUA_OK;
}
```

### 3.3 使用luaL_loadbuffer执行预编译字节码

```c
/* 加载预编译字节码 (.luac) */
int run_lua_bytecode(lua_State *L, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = malloc(size);
    fread(buffer, 1, size, f);
    fclose(f);

    int err = luaL_loadbuffer(L, buffer, size, path) || lua_pcall(L, 0, LUA_MULTRET, 0);
    free(buffer);

    if (err != LUA_OK) {
        fprintf(stderr, "Error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    return err;
}
```

### 3.4 安全沙箱执行

```c
/* 创建受限执行环境 */
lua_State *create_sandbox(void) {
    lua_State *L = luaL_newstate();

    /* 只打开安全的基础库 */
    luaL_openlibs(L);

    /* 移除危险函数 */
    lua_pushnil(L);
    setglobal(L, "dofile");
    lua_pushnil(L);
    setglobal(L, "loadfile");

    /* 可选: 移除 os 库的特定函数 */
    lua_getglobal(L, "os");
    if (lua_type(L, -1) == LUA_TTABLE) {
        lua_pushnil(L);
        lua_setfield(L, -2, "execute");
        lua_pushnil(L);
        lua_setfield(L, -2, "remove");
        lua_pushnil(L);
        lua_setfield(L, -2, "rename");
        lua_pushnil(L);
        lua_setfield(L, -2, "exit");
    }
    lua_pop(L, 1);

    return L;
}

static void setglobal(lua_State *L, const char *name) {
    lua_setglobal(L, name);
}
```

---

## 4. 暴露 C 函数给 Lua

### 4.1 基础函数注册

```c
/* C 函数: 计算两数之和 */
static int l_add(lua_State *L) {
    double a = luaL_checknumber(L, 1);
    double b = luaL_checknumber(L, 2);
    lua_pushnumber(L, a + b);
    return 1; /* 返回值数量 */
}

/* 注册函数 */
void register_c_functions(lua_State *L) {
    lua_register(L, "add", l_add);
    /* 或推入全局表 */
    lua_getglobal(L, "_G");
    lua_pushcfunction(L, l_add);
    lua_setfield(L, -2, "add");
    lua_pop(L, 1);
}
```

### 4.2 使用 luaL_Reg 批量注册

```c
/* 类型安全的包装 */
static int l_vec3_new(lua_State *L) {
    double x = luaL_optnumber(L, 1, 0);
    double y = luaL_optnumber(L, 2, 0);
    double z = luaL_optnumber(L, 3, 0);

    double *vec = lua_newuserdata(L, sizeof(double) * 3);
    vec[0] = x; vec[1] = y; vec[2] = z;

    /* 设置 metatable */
    luaL_getmetatable(L, "vec3");
    lua_setmetatable(L, -2);

    return 1;
}

static int l_vec3_add(lua_State *L) {
    double *a = luaL_checkudata(L, 1, "vec3");
    double *b = luaL_checkudata(L, 2, "vec3");

    double *result = lua_newuserdata(L, sizeof(double) * 3);
    result[0] = a[0] + b[0];
    result[1] = a[1] + b[1];
    result[2] = a[2] + b[2];

    luaL_getmetatable(L, "vec3");
    lua_setmetatable(L, -2);

    return 1;
}

static int l_vec3_tostring(lua_State *L) {
    double *v = luaL_checkudata(L, 1, "vec3");
    lua_pushfstring(L, "vec3(%f, %f, %f)", v[0], v[1], v[2]);
    return 1;
}

/* 注册表 */
static const luaL_Reg vec3_funcs[] = {
    {"new", l_vec3_new},
    {"add", l_vec3_add},
    {NULL, NULL}
};

static const luaL_Reg vec3_meta[] = {
    {"__add", l_vec3_add},
    {"__tostring", l_vec3_tostring},
    {NULL, NULL}
};

void register_vec3(lua_State *L) {
    luaL_newmetatable(L, "vec3");
    luaL_setfuncs(L, vec3_meta, 0);
    lua_pop(L, 1);

    luaL_newlib(L, vec3_funcs);
    lua_setglobal(L, "vec3");
}
```

---

## 5. 暴露 C++ 类给 Lua

### 5.1 面向对象封装

```c
/* C++ 类包装器 (在 .cpp 文件中) */
class Engine {
public:
    Engine(const std::string &name) : name_(name), running_(false) {}
    void Start() { running_ = true; }
    void Stop() { running_ = false; }
    bool IsRunning() const { return running_; }
    const std::string &GetName() const { return name_; }
    void SetName(const std::string &n) { name_ = n; }
private:
    std::string name_;
    bool running_;
};

/* Lua 绑定 (C 接口) */
struct LuaEngine {
    Engine *engine;
};

static int l_engine_new(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);

    LuaEngine *le = lua_newuserdata(L, sizeof(LuaEngine));
    le->engine = new Engine(name);

    luaL_getmetatable(L, "Engine");
    lua_setmetatable(L, -2);

    return 1;
}

static int l_engine_start(lua_State *L) {
    LuaEngine *le = luaL_checkudata(L, 1, "Engine");
    le->engine->Start();
    return 0;
}

static int l_engine_stop(lua_State *L) {
    LuaEngine *le = luaL_checkudata(L, 1, "Engine");
    le->engine->Stop();
    return 0;
}

static int l_engine_is_running(lua_State *L) {
    LuaEngine *le = luaL_checkudata(L, 1, "Engine");
    lua_pushboolean(L, le->engine->IsRunning());
    return 1;
}

static int l_engine_gc(lua_State *L) {
    LuaEngine *le = luaL_checkudata(L, 1, "Engine");
    delete le->engine;
    return 0;
}

static const luaL_Reg engine_funcs[] = {
    {"new", l_engine_new},
    {NULL, NULL}
};

static const luaL_Reg engine_meta[] = {
    {"__gc", l_engine_gc},
    {"__tostring", l_engine_tostring},
    {NULL, NULL}
};

void register_engine(lua_State *L) {
    luaL_newmetatable(L, "Engine");
    lua_pushstring(L, "Engine");
    lua_setfield(L, -2, "__name");
    luaL_setfuncs(L, engine_meta, 0);

    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    luaL_newlib(L, engine_funcs);
    lua_setglobal(L, "Engine");
}
```

### 5.2 使用 LXCLUA 原生 OOP 语法

LXCLUA-NCore 支持将 C 暴露的类包装为 class 使用：

```lua
-- Lua 侧使用
class GameEngine extends C.Engine
    function __init__(self, name)
        C.Engine.new(self, name)
        self.scene = nil
    end

    function loadScene(self, path)
        self.scene = Scene.load(path)
        self:Start()
    end
end

-- 使用
local engine = GameEngine("MyGame")
engine:loadScene("scenes/main.scene")
```

---

## 6. 错误处理

### 6.1 使用 lua_pcall

```c
/* 安全的函数调用 */
int safe_call(lua_State *L, int nargs, int nresults) {
    int err = lua_pcall(L, nargs, nresults, 0);
    if (err != LUA_OK) {
        const char *errmsg = lua_tostring(L, -1);
        /* 处理错误 */
        log_error("Lua error: %s", errmsg);
        /* 获取堆栈跟踪 */
        luaL_traceback(L, L, NULL, 1);
        const char *trace = lua_tostring(L, -1);
        log_error("Traceback:\n%s", trace);
        lua_pop(L, 2); /* 弹出 traceback 和错误消息 */
        return err;
    }
    return LUA_OK;
}
```

### 6.2 使用 lua_xpcall 带上下文

```c
/* 带错误处理的调用 */
typedef struct {
    const char *context;
} ErrorContext;

static int error_handler(lua_State *L) {
    ErrorContext *ctx = lua_touserdata(L, lua_upvalueindex(1));
    luaL_traceback(L, L, NULL, 1);
    lua_pushfstring(L, "[%s] %s: %s", ctx->context, lua_tostring(L, 1), lua_tostring(L, -1));
    return 1;
}

void call_with_context(lua_State *L, const char *ctx, int nargs) {
    ErrorContext ectx = { .context = ctx };
    lua_pushlightuserdata(L, &ectx);
    lua_pushcclosure(L, error_handler, 1);
    int erridx = lua_gettop(L) - nargs - 1;

    int err = lua_pcall(L, nargs, LUA_MULTRET, erridx);
    if (err != LUA_OK) {
        log_error("%s", lua_tostring(L, -1));
    }
    lua_remove(L, erridx); /* 移除错误处理函数 */
}
```

---

## 7. 内存管理

### 7.1 自定义分配器

```c
/* 使用内存池 */
typedef struct {
    size_t allocated;
    size_t limit;
    void *pool_data;
} AllocatorState;

static void *limited_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    AllocatorState *state = (AllocatorState *)ud;

    if (nsize == 0) {
        state->allocated -= osize;
        free(ptr);
        return NULL;
    }

    if (state->allocated - osize + nsize > state->limit) {
        return NULL; /* 超出限制 */
    }

    void *new_ptr = realloc(ptr, nsize);
    if (new_ptr) {
        state->allocated = state->allocated - osize + nsize;
    }
    return new_ptr;
}

lua_State *create_limited_state(size_t memory_limit) {
    AllocatorState *state = malloc(sizeof(AllocatorState));
    state->allocated = 0;
    state->limit = memory_limit;

    lua_State *L = lua_newstate(limited_alloc, state);
    return L;
}
```

### 7.2 GC 调优

```c
/* 配置 GC 参数 */
void configure_gc(lua_State *L) {
    /* 设置 GC pause (默认 200) */
    lua_gc(L, LUA_GCSETPAUSE, 100);

    /* 设置 GC step multiplier (默认 200) */
    lua_gc(L, LUA_GCSETSTEPMUL, 200);

    /* 手动触发完整 GC 周期 */
    lua_gc(L, LUA_GCCOLLECT, 0);

    /* 获取内存使用 (KB) */
    int mem_kb = lua_gc(L, LUA_GCCOUNT, 0);
    printf("Lua memory usage: %d KB\n", mem_kb);
}
```

---

## 8. 安全性最佳实践

### 8.1 代码签名验证

```c
/* 加载已签名的字节码 (SHA-256 + 时间戳加密) */
int load_signed_luac(lua_State *L, const char *path) {
    /* luaL_loadfile 会自动验证签名 */
    int err = luaL_loadfile(L, path);
    if (err == LUA_ERRFILE) {
        fprintf(stderr, "Invalid or corrupted .luac file: %s\n", path);
        return -1;
    }
    /* 执行    lua_pcall(L, 0, LUA_MULTRET, 0); */
    return err;
}
```

### 8.2 资源限制

```c
/* Hook 中断: 防止死循环 */
static void instruction_hook(lua_State *L, lua_Debug *ar) {
    static int count = 0;
    if (++count > 1000000) {
        count = 0;
        luaL_error(L, "instruction limit exceeded");
    }
}

void enable_instruction_limit(lua_State *L) {
    lua_sethook(L, instruction_hook, LUA_MASKCOUNT, 10000);
}
```

### 8.3 输入验证

```c
/* 验证 Lua 函数返回的字符串长度 */
const char *safe_checkstring(lua_State *L, int idx, size_t max_len) {
    size_t len;
    const char *s = luaL_checklstring(L, idx, &len);
    if (len > max_len) {
        luaL_error(L, "string too long (%zu > %zu)", len, max_len);
    }
    return s;
}
```

---

## 9. 完整示例

### 9.1 游戏脚本引擎

```c
/* game_engine.c: 完整的游戏脚本引擎嵌入 */
#include "lxclua.h"

typedef struct {
    lua_State *L;
    int update_ref;
    int render_ref;
} GameScriptEngine;

GameScriptEngine *game_script_init(void) {
    GameScriptEngine *e = calloc(1, sizeof(GameScriptEngine));
    e->L = luaL_newstate();
    luaL_openlibs(e->L);

    /* 注册游戏 API */
    register_game_apis(e->L);

    /* 加载脚本 */
    if (luaL_dofile(e->L, "game/init.lua") != LUA_OK) {
        fprintf(stderr, "Failed to load init.lua: %s\n", lua_tostring(e->L, -1));
    }

    return e;
}

void game_script_update(GameScriptEngine *e, float dt) {
    lua_getglobal(e->L, "Update");
    if (lua_isfunction(e->L, -1)) {
        lua_pushnumber(e->L, dt);
        if (lua_pcall(e->L, 1, 0, 0) != LUA_OK) {
            fprintf(stderr, "Update error: %s\n", lua_tostring(e->L, -1));
            lua_pop(e->L, 1);
        }
    } else {
        lua_pop(e->L, 1);
    }
}

void game_script_render(GameScriptEngine *e) {
    lua_getglobal(e->L, "Render");
    if (lua_isfunction(e->L, -1)) {
        lua_pcall(e->L, 0, 0, 0);
    } else {
        lua_pop(e->L, 1);
    }
}

void game_script_destroy(GameScriptEngine *e) {
    lua_close(e->L);
    free(e);
}

/* main.c */
int main(void) {
    GameScriptEngine *engine = game_script_init();

    /* 简单游戏循环 */
    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60 * 10; i++) { /* 10 seconds */
        game_script_update(engine, dt);
        game_script_render(engine);
        /* sleep(dt) */
    }

    game_script_destroy(engine);
    return 0;
}
```

### 9.2 网络服务脚本

```c
/* web_service.c: 使用 Lua 配置的网络服务 */
#include "lxclua.h"

typedef struct {
    const char *host;
    int port;
    const char *document_root;
} ServerConfig;

static int l_load_config(lua_State *L, const char *path, ServerConfig *cfg) {
    if (luaL_dofile(L, path) != LUA_OK) {
        return -1;
    }

    /* 读取配置表 */
    lua_getglobal(L, "server_config");
    if (lua_type(L, -1) != LUA_TTABLE) {
        lua_pop(L, 1);
        return -1;
    }

    lua_getfield(L, -1, "host");
    cfg->host = luaL_optstring(L, -1, "0.0.0.0");
    lua_pop(L, 1);

    lua_getfield(L, -1, "port");
    cfg->port = (int)luaL_optinteger(L, -1, 8080);
    lua_pop(L, 1);

    lua_getfield(L, -1, "document_root");
    cfg->document_root = luaL_optstring(L, -1, "./www");
    lua_pop(L, 1);

    lua_pop(L, 1);
    return 0;
}
```

---

## 附录 A: C API 快速参考

| 函数 | 说明 |
|------|------|
| `luaL_newstate()` | 创建新 Lua 状态 |
| `luaL_openlibs(L)` | 加载所有标准库 |
| `lua_close(L)` | 销毁 Lua 状态 |
| `luaL_dostring(L, s)` | 执行 Lua 代码字符串 |
| `luaL_dofile(L, fn)` | 执行 Lua 文件 |
| `luaL_loadbuffer(L, buf, sz, name)` | 加载代码缓冲区 |
| `lua_pcall(L, nargs, nresults, errfunc)` | 保护模式调用 |
| `lua_getglobal(L, name)` | 获取全局变量 |
| `lua_setglobal(L, name)` | 设置全局变量 |
| `lua_pushcfunction(L, f)` | 推送 C 函数 |
| `lua_register(L, name, f)` | 注册 C 函数到全局 |
| `luaL_checkstring(L, idx)` | 检查并获取字符串 |
| `luaL_checkinteger(L, idx)` | 检查并获取整数 |
| `luaL_checknumber(L, idx)` | 检查并获取数值 |
| `luaL_optinteger(L, idx, d)` | 可选获取整数 |
| `luaL_optstring(L, idx, d)` | 可选获取字符串 |
| `lua_newuserdata(L, sz)` | 创建 userdata |
| `luaL_newmetatable(L, tname)` | 创建新 metatable |
| `luaL_checkudata(L, idx, tname)` | 检查 userdata 类型 |

## 附录 B: 与标准 Lua 的区别

| 区别 | 标准 Lua 5.4 | LXCLUA-NCore |
|------|-------------|--------------|
| 指令格式 | 32-bit | 64-bit 扰乱 |
| 类型数量 | 9 种 | 15 种 |
| MAXVARS | 250 | 512 |
| OOP 支持 | 无（仅 metatable） | 内置 class/interface/trait |
| 字节码签名 | 无 | SHA-256 |
| 操作码映射 | 固定 | 动态 remapping |
| 扩展库 | 标准库 | 15+ 扩展库 |
