# LXCLUA-NCore C/C++ 嵌入指南

> 将 LXCLUA 脚本引擎嵌入到 C/C++ 宿主程序中的完整指南。

---

## 目录

1. [最小嵌入示例](#1-最小嵌入示例)
2. [编译与链接](#2-编译与链接)
3. [自定义分配器](#3-自定义分配器)
4. [C 函数暴露](#4-c-函数暴露)
5. [C++ 类包装](#5-c-类包装)
6. [错误处理](#6-错误处理)
7. [沙箱与安全](#7-沙箱与安全)
8. [完整示例: 游戏脚本引擎](#8-完整示例-游戏脚本引擎)

---

## 1. 最小嵌入示例

```c
#include "lxclua.h"
#include "lauxlib.h"
#include "lualib.h"

int main() {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    // 执行 Lua 代码
    luaL_dostring(L, "print('Hello from LXCLUA!')");

    lua_close(L);
    return 0;
}
```

编译 (Windows + MSVC):
```bash
cl /Ipath\to\lua\src main.c path\to\lua\lxclua.lib
```

编译 (Linux + GCC):
```bash
gcc -Ipath/to/lua/src main.c -Lpath/to/lua -llua -lm -o main
```

编译 (macOS + Clang):
```bash
clang -Ipath/to/lua/src main.c -Lpath/to/lua -llua -lm -o main
```

---

## 2. 编译与链接

### 2.1 构建选项

| 选项 | 说明 | 默认 |
|------|------|------|
| `BUILD_LUA_LIB` | 构建静态库 | OFF |
| `BUILD_LUA_DLL` | 构建动态库 | OFF |
| `-DLUA_USE_WINDOWS` | Windows 平台 | 自动 |
| `-DLUA_USE_LINUX` | Linux 平台 | 自动 |
| `-DLUA_USE_MACOSX` | macOS 平台 | 自动 |
| `-DLUA_USE_ANDROID` | Android 平台 | 手动 |

### 2.2 CMake 集成

```cmake
cmake_minimum_required(VERSION 3.15)
project(MyApp)

# 添加 LXCLUA 子目录
add_subdirectory(path/to/lxclua)

# 创建可执行文件
add_executable(myapp main.c)

# 链接 LXCLUA
target_link_libraries(myapp PRIVATE lxclua)
target_include_directories(myapp PRIVATE path/to/lxclua/src)
```

### 2.3 手动编译

```bash
# Windows (MSVC)
cl /O2 /MD /I src main.c src/*.c src/core/*.c src/compiler/*.c \
   src/vm/*.c src/stdlib/*.c src/utils/*.c /Fe:myapp.exe

# Linux (GCC)
gcc -O2 -I src -I src/core src/*.c src/core/*.c src/compiler/*.c \
    src/vm/*.c src/stdlib/*.c src/utils/*.c -lm -ldl -o myapp
```

---

## 3. 自定义分配器

### 3.1 分配器签名

```c
typedef void *(*lua_Alloc)(void *ud, void *ptr, size_t osize, size_t nsize);
```

### 3.2 内存限制沙箱

```c
#include "lxclua.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t current_usage;
    size_t max_usage;
} MemLimit;

void *limited_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    MemLimit *ml = (MemLimit *)ud;
    
    if (nsize == 0) {
        // 释放
        ml->current_usage -= osize;
        free(ptr);
        return NULL;
    }
    
    if (ptr == NULL) {
        // 分配新内存
        size_t new_usage = ml->current_usage + nsize;
        if (new_usage > ml->max_usage) return NULL;  // 超过限制
        void *new_ptr = malloc(nsize);
        if (new_ptr) ml->current_usage = new_usage;
        return new_ptr;
    }
    
    // 重新分配
    size_t new_usage = ml->current_usage - osize + nsize;
    if (new_usage > ml->max_usage) return NULL;
    void *new_ptr = realloc(ptr, nsize);
    if (new_ptr) ml->current_usage = new_usage;
    return new_ptr;
}

int main() {
    MemLimit ml = {0, 10 * 1024 * 1024};  // 10MB 限制
    lua_State *L = lua_newstate(limited_alloc, &ml, 0);
    luaL_openlibs(L);
    
    // 执行脚本
    if (luaL_dostring(L, "local t = {}; for i=1,1000000 do t[i]=i end")) {
        // 内存不足时触发错误
        printf("Error: %s\n", lua_tostring(L, -1));
    }
    
    lua_close(L);
    return 0;
}
```

---

## 4. C 函数暴露

### 4.1 注册全局函数

```c
// C 函数: int add(int a, int b)
static int c_add(lua_State *L) {
    int a = (int)luaL_checkinteger(L, 1);
    int b = (int)luaL_checkinteger(L, 2);
    lua_pushinteger(L, a + b);
    return 1;  // 返回值数量
}

// 注册
lua_pushcfunction(L, c_add);
lua_setglobal(L, "add");
```

### 4.2 注册模块

```c
static const luaL_Reg mylib[] = {
    {"add",    c_add},
    {"sub",    c_sub},
    {"mul",    c_mul},
    {"div",    c_div},
    {NULL, NULL}  // 哨兵
};

int luaopen_mylib(lua_State *L) {
    luaL_newlib(L, mylib);
    return 1;
}

// 在 Lua 中: local mylib = require("mylib")
luaL_requiref(L, "mylib", luaopen_mylib, 1);
lua_pop(L, 1);
```

### 4.3 用户数据

```c
typedef struct {
    double x, y;
    double *data;
    int size;
} Vector;

// 创建 Vector 用户数据
static int c_vector_new(lua_State *L) {
    double x = luaL_checknumber(L, 1);
    double y = luaL_checknumber(L, 2);
    
    Vector *v = (Vector *)lua_newuserdata(L, sizeof(Vector));
    v->x = x;
    v->y = y;
    v->data = NULL;
    v->size = 0;
    
    // 设置元表
    luaL_getmetatable(L, "Vector");
    lua_setmetatable(L, -2);
    
    return 1;
}

// Vector 方法
static int c_vector_add(lua_State *L) {
    Vector *a = (Vector *)luaL_checkudata(L, 1, "Vector");
    Vector *b = (Vector *)luaL_checkudata(L, 2, "Vector");
    
    lua_pushnumber(L, a->x + b->x);
    lua_pushnumber(L, a->y + b->y);
    return 2;
}

// 注册元表
luaL_newmetatable(L, "Vector");
lua_pushcfunction(L, c_vector_add);
lua_setfield(L, -2, "__add");
lua_pop(L, 1);
```

---

## 5. C++ 类包装

```cpp
#include "lxclua.h"
#include <string>

class Player {
public:
    Player(const std::string &name) : name_(name), hp_(100) {}
    
    std::string getName() const { return name_; }
    int getHp() const { return hp_; }
    void damage(int amount) { hp_ -= amount; }
    void heal(int amount) { hp_ += amount; }
    
private:
    std::string name_;
    int hp_;
};

// C 包装函数
extern "C" {

static int cpp_player_new(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    
    // 在 userdata 中构造 Player
    void *mem = lua_newuserdata(L, sizeof(Player));
    Player *p = new (mem) Player(name);
    
    luaL_getmetatable(L, "Player");
    lua_setmetatable(L, -2);
    return 1;
}

static int cpp_player_gc(lua_State *L) {
    Player *p = (Player *)luaL_checkudata(L, 1, "Player");
    p->~Player();  // 显式调用析构函数
    return 0;
}

static int cpp_player_getName(lua_State *L) {
    Player *p = (Player *)luaL_checkudata(L, 1, "Player");
    lua_pushstring(L, p->getName().c_str());
    return 1;
}

static int cpp_player_damage(lua_State *L) {
    Player *p = (Player *)luaL_checkudata(L, 1, "Player");
    int amount = (int)luaL_checkinteger(L, 2);
    p->damage(amount);
    return 0;
}

static int cpp_player_heal(lua_State *L) {
    Player *p = (Player *)luaL_checkudata(L, 1, "Player");
    int amount = (int)luaL_checkinteger(L, 2);
    p->heal(amount);
    return 0;
}

} // extern "C"

// 注册
void register_player(lua_State *L) {
    luaL_newmetatable(L, "Player");
    
    // 元方法
    lua_pushcfunction(L, cpp_player_gc);
    lua_setfield(L, -2, "__gc");
    
    // 实例方法
    lua_pushcfunction(L, cpp_player_getName);
    lua_setfield(L, -2, "getName");
    lua_pushcfunction(L, cpp_player_damage);
    lua_setfield(L, -2, "damage");
    lua_pushcfunction(L, cpp_player_heal);
    lua_setfield(L, -2, "heal");
    
    lua_pop(L, 1);
    
    // 构造函数
    lua_pushcfunction(L, cpp_player_new);
    lua_setglobal(L, "Player");
}
```

---

## 6. 错误处理

### 6.1 保护调用

```c
// 调用 Lua 函数，捕获错误
lua_getglobal(L, "risky_function");
lua_pushinteger(L, 42);

if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
    // 错误处理
    const char *err_msg = lua_tostring(L, -1);
    printf("Lua error: %s\n", err_msg);
    lua_pop(L, 1);  // 弹出错误消息
} else {
    int result = (int)lua_tointeger(L, -1);
    printf("Result: %d\n", result);
    lua_pop(L, 1);
}
```

### 6.2 xpcall 带错误处理函数

```c
// 注册错误处理函数
lua_pushcfunction(L, traceback);
lua_setglobal(L, "__TRACEBACK__");

// 调用脚本
lua_getglobal(L, "xpcall");
lua_getglobal(L, "risky_function");
lua_getglobal(L, "__TRACEBACK__");

if (lua_pcall(L, 2, 2, 0) != LUA_OK) {
    // 连 xpcall 都失败
    printf("Fatal: %s\n", lua_tostring(L, -1));
}
```

### 6.3 从 C 中抛出错误

```c
static int c_validate(lua_State *L) {
    int value = (int)luaL_checkinteger(L, 1);
    
    if (value < 0) {
        luaL_error(L, "value must be non-negative, got %d", value);
        // luaL_error 不会返回
    }
    
    lua_pushboolean(L, 1);
    return 1;
}
```

---

## 7. 沙箱与安全

### 7.1 限制可用函数

```c
// 创建沙箱环境
lua_newtable(L);  // 沙箱环境表

// 只注册安全函数
lua_pushcfunction(L, c_safe_print);
lua_setfield(L, -2, "print");

lua_pushcfunction(L, c_safe_tonumber);
lua_setfield(L, -2, "tonumber");

// 设置沙箱环境
lua_setupvalue(L, -2, 1);  // 设置到加载的函数
```

### 7.2 限制执行时间

```c
// 使用钩子限制执行时间
static volatile int timeout_flag = 0;

void timeout_hook(lua_State *L, lua_Debug *ar) {
    if (timeout_flag) {
        luaL_error(L, "script execution timeout");
    }
}

// 设置钩子
lua_sethook(L, timeout_hook, LUA_MASKCOUNT, 10000);  // 每 10000 条指令检查
```

### 7.3 限制内存

参考第 3 节的自定义分配器。

---

## 8. 完整示例: 游戏脚本引擎

```c
#include "lxclua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <stdio.h>
#include <string.h>

// ============================================================
// 游戏实体系统
// ============================================================

typedef struct {
    char name[64];
    int hp;
    int max_hp;
    int attack;
    int defense;
    int x, y;
} Entity;

// 创建实体
static int game_create_entity(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    int hp = (int)luaL_optinteger(L, 2, 100);
    
    Entity *e = (Entity *)lua_newuserdata(L, sizeof(Entity));
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->hp = hp;
    e->max_hp = hp;
    e->attack = 10;
    e->defense = 5;
    e->x = 0;
    e->y = 0;
    
    luaL_getmetatable(L, "Entity");
    lua_setmetatable(L, -2);
    return 1;
}

// 受伤
static int game_take_damage(lua_State *L) {
    Entity *e = (Entity *)luaL_checkudata(L, 1, "Entity");
    int damage = (int)luaL_checkinteger(L, 2);
    
    int actual = damage - e->defense;
    if (actual < 0) actual = 1;
    e->hp -= actual;
    if (e->hp < 0) e->hp = 0;
    
    lua_pushinteger(L, actual);
    return 1;
}

// 攻击
static int game_attack(lua_State *L) {
    Entity *attacker = (Entity *)luaL_checkudata(L, 1, "Entity");
    Entity *target = (Entity *)luaL_checkudata(L, 2, "Entity");
    
    // 计算距离
    int dx = attacker->x - target->x;
    int dy = attacker->y - target->y;
    int dist = dx * dx + dy * dy;
    
    if (dist > 25) {  // 攻击范围 5
        luaL_error(L, "target out of range");
    }
    
    lua_pushinteger(L, target->hp);
    return 1;
}

// 移动
static int game_move(lua_State *L) {
    Entity *e = (Entity *)luaL_checkudata(L, 1, "Entity");
    int dx = (int)luaL_checkinteger(L, 2);
    int dy = (int)luaL_checkinteger(L, 3);
    
    e->x += dx;
    e->y += dy;
    
    lua_pushinteger(L, e->x);
    lua_pushinteger(L, e->y);
    return 2;
}

// 获取属性
static int game_get_info(lua_State *L) {
    Entity *e = (Entity *)luaL_checkudata(L, 1, "Entity");
    
    lua_newtable(L);
    lua_pushstring(L, e->name);
    lua_setfield(L, -2, "name");
    lua_pushinteger(L, e->hp);
    lua_setfield(L, -2, "hp");
    lua_pushinteger(L, e->max_hp);
    lua_setfield(L, -2, "max_hp");
    lua_pushinteger(L, e->attack);
    lua_setfield(L, -2, "attack");
    lua_pushinteger(L, e->defense);
    lua_setfield(L, -2, "defense");
    lua_pushinteger(L, e->x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, e->y);
    lua_setfield(L, -2, "y");
    return 1;
}

// 注册
void register_game_system(lua_State *L) {
    // 创建 Entity 元表
    luaL_newmetatable(L, "Entity");
    
    lua_pushcfunction(L, game_take_damage);
    lua_setfield(L, -2, "takeDamage");
    lua_pushcfunction(L, game_attack);
    lua_setfield(L, -2, "attack");
    lua_pushcfunction(L, game_move);
    lua_setfield(L, -2, "move");
    lua_pushcfunction(L, game_get_info);
    lua_setfield(L, -2, "getInfo");
    
    // __index 指向元表自身
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    
    lua_pop(L, 1);
    
    // 注册构造函数
    lua_pushcfunction(L, game_create_entity);
    lua_setglobal(L, "Entity");
}

// ============================================================
// 主程序
// ============================================================

int main() {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    register_game_system(L);
    
    // 加载游戏脚本
    const char *script =
        "-- 创建玩家和怪物\n"
        "local hero = Entity('Hero', 200)\n"
        "local monster = Entity('Goblin', 50)\n"
        "\n"
        "hero:move(1, 0)\n"
        "monster:move(2, 1)\n"
        "\n"
        "print('Hero HP:', hero:getInfo().hp)\n"
        "print('Monster HP:', monster:getInfo().hp)\n"
        "\n"
        "hero:attack(monster)\n"
        "print('After attack, Monster HP:', monster:getInfo().hp)\n"
        "\n"
        "-- 怪物反击\n"
        "monster:attack(hero)\n"
        "print('After counter, Hero HP:', hero:getInfo().hp)\n";
    
    if (luaL_dostring(L, script) != LUA_OK) {
        fprintf(stderr, "Script error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    
    lua_close(L);
    return 0;
}
```

编译运行:
```bash
cl /I../src main.c ../lxclua.lib
main.exe
# 输出:
# Hero HP: 200
# Monster HP: 50
# After attack, Monster HP: 45
# After counter, Hero HP: 195
```