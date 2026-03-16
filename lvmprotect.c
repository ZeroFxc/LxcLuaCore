#include "lvmprotect.h"
#include <stdio.h>

/* Include cross-platform memory protection headers */
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
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  long page_size = si.dwPageSize;
#else
  long page_size = sysconf(_SC_PAGESIZE);
#endif
  if (page_size <= 0) page_size = 4096;

  /* Align memory page boundaries */
  uintptr_t start_page = (uintptr_t)start & ~(page_size - 1);
  uintptr_t end_page   = ((uintptr_t)end + page_size - 1) & ~(page_size - 1);
  size_t len = end_page - start_page;

  /* Get write permissions (RWX) */
#if defined(_WIN32)
  DWORD oldProtect;
  if (!VirtualProtect((void*)start_page, len, PAGE_EXECUTE_READWRITE, &oldProtect)) return;
#else
  if (mprotect((void*)start_page, len, PROT_READ | PROT_WRITE | PROT_EXEC) == -1) return;
#endif

  uint64_t target = (uint64_t)target_handler;

  /* Write jump/call instructions dynamically based on architecture */
#if defined(__aarch64__) || defined(_M_ARM64)
  for (uint32_t* p = (uint32_t*)start; p < (uint32_t*)end; ++p) {
    if (*p == 0x4C43584C) { /* Little Endian of 'L', 'X', 'C', 'L' */
      uint64_t current = (uint64_t)p;
      int64_t offset = target - current;
      if (offset >= -0x8000000LL && offset <= 0x7FFFFFFLL) {
        uint32_t bl_insn = 0x94000000 | ((offset >> 2) & 0x03FFFFFF); /* BL instruction */
        *p = bl_insn;
      }
    }
  }
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
  for (uint8_t* p = start; p <= end - 8; ++p) {
    if (p[0] == 0x4C && p[1] == 0x58 && p[2] == 0x43 && p[3] == 0x4C &&
        p[4] == 0x4C && p[5] == 0x58 && p[6] == 0x43 && p[7] == 0x4C) {
      uint64_t current = (uint64_t)p;
      int64_t offset = target - (current + 5); /* E8 uses 1 byte, offset uses 4 bytes */
      if (offset >= -0x80000000LL && offset <= 0x7FFFFFFFLL) {
        p[0] = 0xE8; /* CALL */
        p[1] = (offset      ) & 0xFF;
        p[2] = (offset >>  8) & 0xFF;
        p[3] = (offset >> 16) & 0xFF;
        p[4] = (offset >> 24) & 0xFF;
        p[5] = 0x90; /* NOP padding */
        p[6] = 0x90;
        p[7] = 0x90;
      }
      p += 7;
    }
  }
#endif

  /* Flush CPU instruction cache */
  __builtin___clear_cache((char*)start_page, (char*)start_page + len);

  /* Restore execution permissions (RX) */
#if defined(_WIN32)
  VirtualProtect((void*)start_page, len, oldProtect, &oldProtect);
#else
  mprotect((void*)start_page, len, PROT_READ | PROT_EXEC);
#endif
}
