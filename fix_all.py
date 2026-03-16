import re
import os

with open('lvmprotect.h', 'w') as f:
    f.write("""#ifndef lvmprotect_h
#define lvmprotect_h

#include <stddef.h>
#include <stdint.h>

#if defined(__APPLE__)
#define ASM_GLOBAL ".globl"
#define ASM_PREFIX "_"
#elif defined(_WIN32) || defined(__CYGWIN__)
#define ASM_GLOBAL ".globl"
#if defined(__x86_64__) || defined(__aarch64__)
#define ASM_PREFIX ""
#else
#define ASM_PREFIX "_"
#endif
#else
#define ASM_GLOBAL ".global"
#define ASM_PREFIX ""
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#define VMP_BYTES ".byte 0x4C, 0x58, 0x43, 0x4C\\n.byte 0x4C, 0x58, 0x43, 0x4C\\n.byte 0x90, 0x90\\n"
#else
#define VMP_BYTES ".byte 0x4C, 0x58, 0x43, 0x4C\\n.byte 0x4C, 0x58, 0x43, 0x4C\\n"
#endif

#define VMP_MARKER(name) \\
  __asm__( \\
    ASM_GLOBAL " " ASM_PREFIX #name "_start\\n" \\
    ASM_PREFIX #name "_start:\\n" \\
    VMP_BYTES \\
    ASM_GLOBAL " " ASM_PREFIX #name "_end\\n" \\
    ASM_PREFIX #name "_end:\\n" \\
  )

#define DECLARE_VMP_MARKER(name) \\
  extern char name##_start[]; \\
  extern char name##_end[]

void vmp_patch_memory(void* start, void* end, void* target_handler);

#endif
""")

with open('lvmprotect.c', 'w') as f:
    f.write("""#include "lvmprotect.h"
#include <stdio.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

void vmp_patch_memory(void* start_ptr, void* end_ptr, void* target_handler) {
  uint8_t* start = (uint8_t*)start_ptr;
  uint8_t* end   = (uint8_t*)end_ptr;
  if (start >= end) return;
#if defined(_WIN32)
  SYSTEM_INFO si; GetSystemInfo(&si); long page_size = si.dwPageSize;
#else
  long page_size = sysconf(_SC_PAGESIZE);
#endif
  if (page_size <= 0) page_size = 4096;
  uintptr_t start_page = (uintptr_t)start & ~(page_size - 1);
  uintptr_t end_page   = ((uintptr_t)end + page_size - 1) & ~(page_size - 1);
  size_t len = end_page - start_page;
#if defined(_WIN32)
  DWORD oldProtect; VirtualProtect((void*)start_page, len, PAGE_EXECUTE_READWRITE, &oldProtect);
#else
  mprotect((void*)start_page, len, PROT_READ | PROT_WRITE | PROT_EXEC);
#endif
  uint64_t target = (uint64_t)target_handler;
#if defined(__aarch64__) || defined(_M_ARM64)
  for (uint32_t* p = (uint32_t*)start; p < (uint32_t*)end; ++p) {
    if (*p == 0x4C43584C) {
      uint64_t current = (uint64_t)p;
      int64_t offset = target - current;
      if (offset >= -0x8000000LL && offset <= 0x7FFFFFFLL) { *p = 0x94000000 | ((offset >> 2) & 0x03FFFFFF); }
    }
  }
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
  for (uint8_t* p = start; p <= end - 8; ++p) {
    if (p[0] == 0x4C && p[1] == 0x58 && p[2] == 0x43 && p[3] == 0x4C && p[4] == 0x4C && p[5] == 0x58 && p[6] == 0x43 && p[7] == 0x4C) {
      uint64_t current = (uint64_t)p;
      int64_t offset = target - (current + 5);
      if (offset >= -0x80000000LL && offset <= 0x7FFFFFFFLL) {
        p[0] = 0xE8; p[1] = offset & 0xFF; p[2] = (offset >> 8) & 0xFF; p[3] = (offset >> 16) & 0xFF; p[4] = (offset >> 24) & 0xFF;
        p[5] = 0x90; p[6] = 0x90; p[7] = 0x90;
      }
      p += 7;
    }
  }
#endif
  __builtin___clear_cache((char*)start_page, (char*)start_page + len);
#if defined(_WIN32)
  VirtualProtect((void*)start_page, len, oldProtect, &oldProtect);
#else
  mprotect((void*)start_page, len, PROT_READ | PROT_EXEC);
#endif
}
""")

with open('lundump.c', 'r') as f:
    c = f.read()

handler_code_simple = """
#include <stdlib.h>
int g_is_illegal_environment = 0;
__attribute__((used))
static void lundump_security_handler() {
  void *caller = __builtin_return_address(0);
  if (g_is_illegal_environment) {
    printf("[Anti-Dump VMP] Warning: Detected illegal hook/dump environment (Source: %p)\\n", caller);
    printf("[Anti-Dump VMP] Cutting off memory access... (Intercepting illegal dump)\\n");
    exit(1);
  }
}
"""

c = re.sub(
    r'__attribute__\(\(used\)\)\nstatic void lundump_security_handler\(\) \{.*?\}',
    handler_code_simple,
    c,
    flags=re.DOTALL
)
with open('lundump.c', 'w') as f: f.write(c)
