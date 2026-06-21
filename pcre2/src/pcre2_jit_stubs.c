/*
** PCRE2 JIT 桩函数 — 用于不支持 JIT 的平台 (wasm 等)
** 当 SUPPORT_JIT 已定义但 pcre2_jit_compile.c 未被编译时，提供这些桩函数
*/

#include "pcre2_internal.h"


/* 桩函数：pcre2_jit_compile — 返回不支持 JIT 的错误码 */
PCRE2_EXP_DEFN int PCRE2_CALL_CONVENTION
pcre2_jit_compile(pcre2_code *code, uint32_t options)
{
  (void)code;
  (void)options;
  return PCRE2_ERROR_JIT_BADOPTION;
}


/* 桩函数：pcre2_jit_match — 返回不支持 JIT 的错误码 */
PCRE2_EXP_DEFN int PCRE2_CALL_CONVENTION
pcre2_jit_match(const pcre2_code *code, PCRE2_SPTR subject, PCRE2_SIZE length,
                PCRE2_SIZE start_offset, uint32_t options,
                pcre2_match_data *match_data, pcre2_match_context *mcontext)
{
  (void)code;
  (void)subject;
  (void)length;
  (void)start_offset;
  (void)options;
  (void)match_data;
  (void)mcontext;
  return PCRE2_ERROR_JIT_BADOPTION;
}


/* 桩函数：_pcre2_jit_free — 空操作 */
void _pcre2_jit_free(void *ptr, pcre2_memctl *memctl)
{
  (void)ptr;
  (void)memctl;
}


/* 桩函数：_pcre2_jit_free_rodata — 空操作 */
void _pcre2_jit_free_rodata(void *ptr, void *allocator_data)
{
  (void)ptr;
  (void)allocator_data;
}


/* 桩函数：_pcre2_jit_get_size — 返回 0 */
size_t _pcre2_jit_get_size(void *ptr)
{
  (void)ptr;
  return 0;
}


/* 桩函数：_pcre2_jit_get_target — 返回 "nojit" */
const char *_pcre2_jit_get_target(void)
{
  return "nojit";
}


/* 桩函数：pcre2_jit_free_unused_memory — 空操作 */
PCRE2_EXP_DEFN void PCRE2_CALL_CONVENTION
pcre2_jit_free_unused_memory(pcre2_general_context *gcontext)
{
  (void)gcontext;
}