/*
** LXCLUA JIT 变量桩 — 用于 wasm 等不支持 JIT 的平台
** 定义 XCLUA_PCRE2_ENABLED 和 XCLUA_REGEX_JIT_ENABLED 为 0，
** 确保 lstrlib.c 中的 PCRE2 JIT 代码路径不会被触发
*/

int XCLUA_PCRE2_ENABLED = 0;
int XCLUA_REGEX_JIT_ENABLED = 0;