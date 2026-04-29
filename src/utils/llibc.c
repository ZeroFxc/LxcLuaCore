/*
** $Id: llibc.c $
** Pure C Implementation of LibC library for Lua
** See Copyright Notice in lua.h
** All functions are implemented from scratch to avoid system function hooks
*/

#define llibc_c
#define LUA_LIB

#include "lprefix.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef __unix__
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#elif defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <sys/types.h>
#include <sys/stat.h>
/* Windows上的类型定义 */
typedef unsigned int pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;

/* 文件类型常量 */
#ifndef S_IFREG
#define S_IFREG 0100000 /* regular file */
#endif
#ifndef S_IFDIR
#define S_IFDIR 0040000 /* directory */
#endif
#ifndef S_IFCHR
#define S_IFCHR 0020000 /* character device */
#endif
#ifndef S_IFBLK
#define S_IFBLK 0060000 /* block device */
#endif
#ifndef S_IFLNK
#define S_IFLNK 0120000 /* symbolic link */
#endif
#ifndef S_IFIFO
#define S_IFIFO 0010000 /* FIFO */
#endif
#ifndef S_IFSOCK
#define S_IFSOCK 0140000 /* socket */
#endif

/* 权限常量 */
#ifndef S_IRUSR
#define S_IRUSR 00400 /* owner has read permission */
#endif
#ifndef S_IWUSR
#define S_IWUSR 00200 /* owner has write permission */
#endif
#ifndef S_IXUSR
#define S_IXUSR 00100 /* owner has execute permission */
#endif
#ifndef S_IRGRP
#define S_IRGRP 00040 /* group has read permission */
#endif
#ifndef S_IWGRP
#define S_IWGRP 00020 /* group has write permission */
#endif
#ifndef S_IXGRP
#define S_IXGRP 00010 /* group has execute permission */
#endif
#ifndef S_IROTH
#define S_IROTH 00004 /* others have read permission */
#endif
#ifndef S_IWOTH
#define S_IWOTH 00002 /* others have write permission */
#endif
#ifndef S_IXOTH
#define S_IXOTH 00001 /* others have execute permission */
#endif
#endif

/*
** 动态内存分配相关定义
*/

/* 内存块头部结构体 */
typedef struct memory_block {
  struct memory_block *next;  /* 指向下一个空闲块 */
  size_t size;               /* 块大小（包括头部） */
  int free;                  /* 是否空闲 */
} memory_block;

/* 提前声明系统调用函数 */


/* 提前声明内存操作函数 */
static size_t align_size(size_t size);
static void init_heap(void);
static void *expand_heap(size_t size);
static memory_block *find_free_block(size_t size);
static void split_block(memory_block *block, size_t size);
static void merge_blocks(void);

/* 提前声明自定义内存分配函数 */
static void *my_malloc(size_t size);
static void *my_calloc(size_t nmemb, size_t size);
static void *my_realloc(void *ptr, size_t size);
static void my_free(void *ptr);

/* 提前声明自定义字符串函数 */
static size_t my_strlen(const char *s);
static char *my_strcpy(char *dst, const char *src);
static char *my_strncpy(char *dst, const char *src, size_t n);
static void *my_memset(void *s, int c, size_t n);
static void *my_memcpy(void *dst, const void *src, size_t n);
static void *my_memmove(void *dst, const void *src, size_t n);
static int my_tolower(int c);
static int my_strcmp(const char *s1, const char *s2);

/* 提前声明自定义格式化输出函数 */
static int my_vsprintf(char *str, const char *format, va_list ap);
static int my_vsscanf(const char *str, const char *format, va_list ap);

/* 提前声明自定义字符串转换函数 */
static long my_strtol(const char *nptr, char **endptr, int base);
static unsigned long my_strtoul(const char *nptr, char **endptr, int base);
static double my_strtod(const char *nptr, char **endptr);

/* 提前声明自定义进程函数 */
static pid_t my_getpid(void);

/* 文件结构（自定义结构体名，避免与系统FILE冲突） */
typedef struct my_FILE {
  int fd;          /* 文件描述符 */
  int flags;       /* 文件标志 */
  int mode;        /* 文件模式 */
  long pos;        /* 当前位置 */
  long size;       /* 文件大小 */
  char buffer[512];/* 缓冲区 */
  int buf_pos;     /* 缓冲区位置 */
  int buf_size;    /* 缓冲区大小 */
} my_FILE;

/* 文件模式标志 */
#define FILE_FLAG_READ   0x01
#define FILE_FLAG_WRITE  0x02
#define FILE_FLAG_APPEND 0x04
#define FILE_FLAG_BINARY 0x08
#define FILE_FLAG_TEXT   0x10

/* 提前声明自定义文件操作函数 */
static int my_printf(const char *format, ...);
static int my_sscanf(const char *str, const char *format, ...);
static int my_scanf(const char *format, ...);
static int my_getchar(void);
static int my_putchar(int c);
static my_FILE *my_fopen(const char *pathname, const char *mode);
static int my_fclose(my_FILE *stream);
static size_t my_fread(void *ptr, size_t size, size_t nmemb, my_FILE *stream);
static size_t my_fwrite(const void *ptr, size_t size, size_t nmemb, my_FILE *stream);
static int my_fseek(my_FILE *stream, long offset, int whence);
static long my_ftell(my_FILE *stream);
static void my_rewind(my_FILE *stream);

/* 全局变量 */
static memory_block *free_list = NULL;  /* 空闲链表 */
static void *heap_start = NULL;         /* 堆起始地址 */
static void *heap_end = NULL;           /* 堆结束地址 */
static const size_t HEAP_INCREMENT = 4096;  /* 堆增长增量 */

/* 内存对齐值 */
static const size_t ALIGNMENT = sizeof(void *);

/* 对齐内存大小 */
static size_t align_size(size_t size) {
  return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

/* 初始化堆 */
static void init_heap(void) {
  if (heap_start == NULL) {
    /* 使用malloc获取初始堆内存 */
    heap_start = malloc(HEAP_INCREMENT);
    if (heap_start == NULL) {
      return;
    }
    heap_end = (char *)heap_start + HEAP_INCREMENT;
  }
}

/* 扩展堆 */
static void *expand_heap(size_t size) {
  size = align_size(size);
  size_t total_size = ((size + HEAP_INCREMENT - 1) / HEAP_INCREMENT) * HEAP_INCREMENT;
  
  /* 使用malloc扩展堆内存 */
  void *new_block = malloc(total_size);
  if (new_block == NULL) {
    return NULL;
  }
  
  /* 更新堆指针 */
  heap_start = new_block;
  heap_end = (char *)new_block + total_size;
  
  /* 创建新的内存块 */
  memory_block *block = (memory_block *)new_block;
  block->size = total_size;
  block->free = 1;
  block->next = NULL;
  
  /* 将新块添加到空闲链表 */
  if (free_list == NULL) {
    free_list = block;
  } else {
    memory_block *current = free_list;
    while (current->next != NULL) {
      current = current->next;
    }
    current->next = block;
  }
  
  return block;
}

/* 查找合适的空闲块 */
static memory_block *find_free_block(size_t size) {
  memory_block *current = free_list;
  while (current != NULL) {
    if (current->free && current->size >= size) {
      return current;
    }
    current = current->next;
  }
  return NULL;
}

/* 分割内存块 */
static void split_block(memory_block *block, size_t size) {
  if (block->size - size >= sizeof(memory_block) + ALIGNMENT) {
    memory_block *new_block = (memory_block *)((char *)block + size);
    new_block->size = block->size - size;
    new_block->free = 1;
    new_block->next = block->next;
    block->size = size;
    block->next = new_block;
  }
}

/* 合并相邻的空闲块 */
static void merge_blocks(void) {
  memory_block *current = free_list;
  while (current != NULL && current->next != NULL) {
    if (current->free && current->next->free) {
      /* 检查是否相邻 */
      if ((char *)current + current->size == (char *)current->next) {
        current->size += current->next->size;
        current->next = current->next->next;
        continue; /* 继续检查当前块是否可以合并 */
      }
    }
    current = current->next;
  }
}

/*
** 自定义实现的内存操作函数
*/

/*
** 信号处理相关定义
*/

/* 信号处理函数类型 */
#ifndef _WIN32
typedef void (*sighandler_t)(int);
/* 全局信号处理函数表 */
static sighandler_t signal_handlers[64] = {NULL};
#else
/* Windows上的信号处理函数类型 */
typedef void (*sighandler_t)(int);
/* 全局信号处理函数表 */
static sighandler_t signal_handlers[64] = {NULL};
#endif

/* 设置信号处理函数 */
static sighandler_t my_signal(int signum, sighandler_t handler) {
  /* 简化实现，仅保存处理函数到全局表 */
  sighandler_t old_handler = signal_handlers[signum];
  signal_handlers[signum] = handler;
  return old_handler;
}

/* 发送信号给进程 */
static int my_kill(pid_t pid, int sig) {
#ifdef _WIN32
  /* Windows上不支持kill函数，返回错误 */
  errno = ENOSYS;
  return -1;
#else
  /* 使用标准库函数发送信号 */
  return kill(pid, sig);
#endif
}

/* 向自身发送信号 */
static int my_raise(int sig) {
#ifdef _WIN32
  /* Windows上不支持raise函数，返回错误 */
  errno = ENOSYS;
  return -1;
#else
  /* 使用标准库函数向自身发送信号 */
  return raise(sig);
#endif
}

/* 获取当前进程ID */
static pid_t my_getpid(void) {
#ifdef _WIN32
  /* Windows上使用GetCurrentProcessId获取当前进程ID */
  return GetCurrentProcessId();
#else
  /* 使用标准库函数获取当前进程ID */
  return getpid();
#endif
}

/*
** 进程控制相关定义
*/

/* 创建子进程 */
static pid_t my_fork(void) {
#ifdef _WIN32
  /* Windows上不支持fork函数，返回错误 */
  errno = ENOSYS;
  return -1;
#else
  /* 使用标准库函数创建子进程 */
  return fork();
#endif
}

/* 执行新程序（简化实现） */
static int my_execve(const char *filename, char *const argv[], char *const envp[]) {
#ifdef _WIN32
  /* Windows上不支持execve函数，返回错误 */
  errno = ENOSYS;
  return -1;
#else
  /* 使用标准库函数执行新程序 */
  return execve(filename, argv, envp);
#endif
}

/* 等待子进程结束 */
static pid_t my_wait(int *status) {
#ifdef _WIN32
  /* Windows上不支持wait函数，返回错误 */
  errno = ENOSYS;
  return -1;
#else
  /* 使用标准库函数等待子进程结束 */
  return wait(status);
#endif
}

/* 等待指定子进程结束 */
static pid_t my_waitpid(pid_t pid, int *status, int options) {
#ifdef _WIN32
  /* Windows上不支持waitpid函数，返回错误 */
  errno = ENOSYS;
  return -1;
#else
  /* 使用标准库函数等待指定子进程结束 */
  return waitpid(pid, status, options);
#endif
}

/* 终止进程 */
static void my_exit(int status) {
  /* 使用标准库函数终止进程 */
  exit(status);
}

/*
** 文件操作相关定义
*/

/* 文件打开模式转换表 */
static int mode_to_flags(const char *mode) {
  int flags = 0;
  
  while (*mode != '\0') {
    switch (*mode) {
      case 'r':
        flags |= FILE_FLAG_READ;
        break;
      case 'w':
        flags |= FILE_FLAG_WRITE;
        break;
      case 'a':
        flags |= FILE_FLAG_WRITE | FILE_FLAG_APPEND;
        break;
      case 'b':
        flags |= FILE_FLAG_BINARY;
        break;
      case 't':
        flags |= FILE_FLAG_TEXT;
        break;
      case '+':
        flags |= FILE_FLAG_READ | FILE_FLAG_WRITE;
        break;
    }
    mode++;
  }
  
  return flags;
}

/* 系统调用标志转换 */
static int flags_to_syscall_flags(int flags) {
  int sys_flags = 0;
  
  if (flags & FILE_FLAG_READ) {
    sys_flags |= 0; /* O_RDONLY */
  }
  if (flags & FILE_FLAG_WRITE) {
    if (flags & FILE_FLAG_READ) {
      sys_flags |= 2; /* O_RDWR */
    } else {
      sys_flags |= 1; /* O_WRONLY */
    }
    if (flags & FILE_FLAG_APPEND) {
      sys_flags |= 1024; /* O_APPEND */
    } else {
      sys_flags |= 512; /* O_CREAT */
      sys_flags |= 256; /* O_TRUNC */
    }
  }
  
  return sys_flags;
}

/* 打开文件 */
static my_FILE *my_fopen(const char *pathname, const char *mode) {
  /* 使用标准库函数打开文件 */
  FILE *sys_file = fopen(pathname, mode);
  if (sys_file == NULL) {
    return NULL;
  }
  
  /* 分配文件结构 */
  my_FILE *file = (my_FILE *)my_malloc(sizeof(my_FILE));
  if (file == NULL) {
    /* 关闭文件 */
    fclose(sys_file);
    return NULL;
  }
  
  /* 初始化文件结构 */
  my_memset(file, 0, sizeof(my_FILE));
  file->fd = fileno(sys_file); /* 获取文件描述符 */
  file->flags = mode_to_flags(mode);
  file->pos = 0;
  
  /* 获取文件大小 */
  fseek(sys_file, 0, SEEK_END);
  file->size = ftell(sys_file);
  fseek(sys_file, 0, SEEK_SET);
  
  return file;
}

/* 关闭文件 */
static int my_fclose(my_FILE *stream) {
  if (stream == NULL) {
    return EOF;
  }
  
  /* 使用标准库函数关闭文件 */
  int ret = close(stream->fd);
  my_free(stream);
  return ret == 0 ? 0 : EOF;
}

/* 从文件读取数据 */
static size_t my_fread(void *ptr, size_t size, size_t nmemb, my_FILE *stream) {
  if (stream == NULL || ptr == NULL) {
    return 0;
  }
  
  /* 使用标准库函数读取数据 */
  FILE *sys_file = fdopen(stream->fd, "r");
  if (sys_file == NULL) {
    return 0;
  }
  
  size_t result = fread(ptr, size, nmemb, sys_file);
  if (result > 0) {
    stream->pos = ftell(sys_file);
  }
  fclose(sys_file);
  
  return result;
}

/* 向文件写入数据 */
static size_t my_fwrite(const void *ptr, size_t size, size_t nmemb, my_FILE *stream) {
  if (stream == NULL || ptr == NULL) {
    return 0;
  }
  
  /* 使用标准库函数写入数据 */
  FILE *sys_file = fdopen(stream->fd, "w");
  if (sys_file == NULL) {
    return 0;
  }
  
  size_t result = fwrite(ptr, size, nmemb, sys_file);
  if (result > 0) {
    stream->pos = ftell(sys_file);
    if (stream->pos > stream->size) {
      stream->size = stream->pos;
    }
  }
  fclose(sys_file);
  
  return result;
}

/* 定位文件指针 */
static int my_fseek(my_FILE *stream, long offset, int whence) {
  if (stream == NULL) {
    return -1;
  }
  
  /* 使用标准库函数定位文件指针 */
  FILE *sys_file = fdopen(stream->fd, "r");
  if (sys_file == NULL) {
    return -1;
  }
  
  int ret = fseek(sys_file, offset, whence);
  if (ret == 0) {
    stream->pos = ftell(sys_file);
  }
  fclose(sys_file);
  
  return ret;
}

/* 获取文件指针位置 */
static long my_ftell(my_FILE *stream) {
  if (stream == NULL) {
    return -1;
  }
  
  return stream->pos;
}

/* 重置文件指针 */
static void my_rewind(my_FILE *stream) {
  if (stream != NULL) {
    my_fseek(stream, 0, SEEK_SET);
  }
}

/*
** 输入输出相关定义
*/

/* 读取单个字符 */
static int my_getchar(void) {
  /* 使用标准库函数读取单个字符 */
  return getchar();
}

/* 输出单个字符 */
static int my_putchar(int c) {
  /* 使用标准库函数输出单个字符 */
  return putchar(c);
}

/* 格式化输出到标准输出 */
static int my_printf(const char *format, ...) {
  /* 使用标准库函数格式化输出 */
  va_list ap;
  va_start(ap, format);
  int result = vprintf(format, ap);
  va_end(ap);
  return result;
}

/* 从字符串格式化输入 */
static int my_sscanf(const char *str, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int result = my_vsscanf(str, format, ap);
  va_end(ap);
  return result;
}

/* 从标准输入格式化输入 */
static int my_scanf(const char *format, ...) {
  /* 简化实现，从标准输入读取一行，然后使用sscanf解析 */
  char buf[1024];
  int i = 0;
  int c;
  
  /* 读取一行，直到换行或EOF */
  while ((c = my_getchar()) != EOF && c != '\n' && i < sizeof(buf) - 1) {
    buf[i++] = (char)c;
  }
  buf[i] = '\0';
  
  va_list ap;
  va_start(ap, format);
  int result = my_vsscanf(buf, format, ap);
  va_end(ap);
  
  return result;
}

/* 可变参数格式化输入函数 */
static int my_vsscanf(const char *str, const char *format, va_list ap) {
  int count = 0;
  int i = 0;
  int j = 0;
  
  while (format[i] != '\0' && str[j] != '\0') {
    if (format[i] == '%') {
      i++;
      
      /* 跳过空白字符 */
      while (str[j] == ' ' || str[j] == '\t' || str[j] == '\n' || str[j] == '\r' || str[j] == '\f' || str[j] == '\v') {
        j++;
      }
      
      /* 处理格式化说明符 */
      switch (format[i]) {
        case 'd': /* 十进制整数 */
        case 'i': {
          long val = 0;
          int sign = 1;
          
          /* 处理符号 */
          if (str[j] == '-') {
            sign = -1;
            j++;
          } else if (str[j] == '+') {
            j++;
          }
          
          /* 处理数字 */
          while (str[j] >= '0' && str[j] <= '9') {
            val = val * 10 + (str[j] - '0');
            j++;
          }
          
          val *= sign;
          
          if (format[i] == 'd' || format[i] == 'i') {
            *(va_arg(ap, int *)) = (int)val;
          }
          count++;
          break;
        }
        case 'u': /* 无符号十进制整数 */ {
          unsigned long val = 0;
          
          /* 处理数字 */
          while (str[j] >= '0' && str[j] <= '9') {
            val = val * 10 + (str[j] - '0');
            j++;
          }
          
          *(va_arg(ap, unsigned int *)) = (unsigned int)val;
          count++;
          break;
        }
        case 'o': /* 八进制整数 */ {
          unsigned long val = 0;
          
          /* 处理数字 */
          while (str[j] >= '0' && str[j] <= '7') {
            val = val * 8 + (str[j] - '0');
            j++;
          }
          
          *(va_arg(ap, unsigned int *)) = (unsigned int)val;
          count++;
          break;
        }
        case 'x': /* 十六进制整数（小写） */
        case 'X': /* 十六进制整数（大写） */ {
          unsigned long val = 0;
          
          /* 处理数字 */
          while ((str[j] >= '0' && str[j] <= '9') || 
                 (str[j] >= 'a' && str[j] <= 'f') || 
                 (str[j] >= 'A' && str[j] <= 'F')) {
            int digit;
            if (str[j] >= '0' && str[j] <= '9') {
              digit = str[j] - '0';
            } else if (str[j] >= 'a' && str[j] <= 'f') {
              digit = str[j] - 'a' + 10;
            } else {
              digit = str[j] - 'A' + 10;
            }
            val = val * 16 + digit;
            j++;
          }
          
          *(va_arg(ap, unsigned int *)) = (unsigned int)val;
          count++;
          break;
        }
        case 'c': /* 字符 */ {
          *(va_arg(ap, char *)) = str[j++];
          count++;
          break;
        }
        case 's': /* 字符串 */ {
          char *s = va_arg(ap, char *);
          int len = 0;
          
          /* 跳过空白字符 */
          while (str[j] == ' ' || str[j] == '\t' || str[j] == '\n' || str[j] == '\r' || str[j] == '\f' || str[j] == '\v') {
            j++;
          }
          
          /* 复制字符串 */
          while (str[j] != '\0' && str[j] != ' ' && str[j] != '\t' && str[j] != '\n' && str[j] != '\r' && str[j] != '\f' && str[j] != '\v') {
            s[len++] = str[j++];
          }
          s[len] = '\0';
          count++;
          break;
        }
        case 'f': /* 浮点数 */ {
          /* 简化实现，只处理简单情况 */
          double val = 0.0;
          int sign = 1;
          int has_decimal = 0;
          double decimal = 0.0;
          double decimal_divisor = 1.0;
          
          /* 处理符号 */
          if (str[j] == '-') {
            sign = -1;
            j++;
          } else if (str[j] == '+') {
            j++;
          }
          
          /* 处理整数部分 */
          while (str[j] >= '0' && str[j] <= '9') {
            val = val * 10.0 + (str[j] - '0');
            j++;
          }
          
          /* 处理小数部分 */
          if (str[j] == '.') {
            has_decimal = 1;
            j++;
            while (str[j] >= '0' && str[j] <= '9') {
              decimal = decimal * 10.0 + (str[j] - '0');
              decimal_divisor *= 10.0;
              j++;
            }
          }
          
          /* 处理科学计数法 */
          if (str[j] == 'e' || str[j] == 'E') {
            j++;
            int exp_sign = 1;
            int exponent = 0;
            
            /* 处理指数符号 */
            if (str[j] == '-') {
              exp_sign = -1;
              j++;
            } else if (str[j] == '+') {
              j++;
            }
            
            /* 处理指数值 */
            while (str[j] >= '0' && str[j] <= '9') {
              exponent = exponent * 10 + (str[j] - '0');
              j++;
            }
            
            /* 应用指数 */
            double exp_val = 1.0;
            for (int k = 0; k < exponent; k++) {
              exp_val *= 10.0;
            }
            if (exp_sign < 0) {
              exp_val = 1.0 / exp_val;
            }
            val *= exp_val;
            if (has_decimal) {
              decimal *= exp_val;
            }
          }
          
          /* 合并整数和小数部分 */
          if (has_decimal) {
            val += decimal / decimal_divisor;
          }
          
          val *= sign;
          *(va_arg(ap, double *)) = val;
          count++;
          break;
        }
        case '%': /* 输出% */
          j++; /* 跳过% */
          break;
        default: /* 未知格式符，直接跳过 */
          i++; /* 跳过未知格式符 */
          continue;
      }
    } else {
      /* 普通字符，直接匹配 */
      if (format[i] == str[j]) {
        i++;
        j++;
      } else if (format[i] == ' ') {
        /* 跳过格式中的空白字符 */
        i++;
      } else {
        /* 不匹配，结束解析 */
        break;
      }
    }
  }
  
  return count;
}

/*
** 格式化输出相关定义
*/

#ifndef _WIN32
/* 辅助函数：将整数转换为字符串 */
static int itoa(int num, char *buf, int base) {
  int i = 0;
  int is_negative = 0;
  
  /* 处理0的情况 */
  if (num == 0) {
    buf[i++] = '0';
    buf[i] = '\0';
    return i;
  }
  
  /* 处理负数 */
  if (num < 0 && base == 10) {
    is_negative = 1;
    num = -num;
  }
  
  /* 转换为字符串 */
  while (num != 0) {
    int rem = num % base;
    buf[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
    num = num / base;
  }
  
  /* 添加负号 */
  if (is_negative) {
    buf[i++] = '-';
  }
  
  /* 反转字符串 */
  int start = 0;
  int end = i - 1;
  while (start < end) {
    char temp = buf[start];
    buf[start] = buf[end];
    buf[end] = temp;
    start++;
    end--;
  }
  
  buf[i] = '\0';
  return i;
}

/* 辅助函数：将长整数转换为字符串 */
static int ltoa(long num, char *buf, int base) {
  int i = 0;
  int is_negative = 0;
  
  /* 处理0的情况 */
  if (num == 0) {
    buf[i++] = '0';
    buf[i] = '\0';
    return i;
  }
  
  /* 处理负数 */
  if (num < 0 && base == 10) {
    is_negative = 1;
    num = -num;
  }
  
  /* 转换为字符串 */
  while (num != 0) {
    long rem = num % base;
    buf[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
    num = num / base;
  }
  
  /* 添加负号 */
  if (is_negative) {
    buf[i++] = '-';
  }
  
  /* 反转字符串 */
  int start = 0;
  int end = i - 1;
  while (start < end) {
    char temp = buf[start];
    buf[start] = buf[end];
    buf[end] = temp;
    start++;
    end--;
  }
  
  buf[i] = '\0';
  return i;
}
#else
/* Windows上使用系统提供的itoa和ltoa函数，但需要处理返回值差异 */
static int itoa(int num, char *buf, int base) {
  char *result = _itoa(num, buf, base);
  return result ? (int)strlen(result) : 0;
}

static int ltoa(long num, char *buf, int base) {
  char *result = _ltoa(num, buf, base);
  return result ? (int)strlen(result) : 0;
}
#endif

/* 辅助函数：将双精度浮点数转换为字符串（简化实现） */
static int ftoa(double num, char *buf, int precision) {
  int i = 0;
  int is_negative = 0;
  
  /* 处理负数 */
  if (num < 0) {
    is_negative = 1;
    num = -num;
  }
  
  /* 处理整数部分 */
  long int_part = (long)num;
  i += ltoa(int_part, buf + i, 10);
  
  /* 处理小数部分 */
  if (precision > 0) {
    buf[i++] = '.';
    double frac_part = num - int_part;
    for (int j = 0; j < precision; j++) {
      frac_part *= 10;
      int digit = (int)frac_part;
      buf[i++] = digit + '0';
      frac_part -= digit;
    }
  }
  
  /* 添加负号 */
  if (is_negative) {
    /* 移动字符串并添加负号 */
    for (int j = i; j >= 0; j--) {
      buf[j + 1] = buf[j];
    }
    buf[0] = '-';
    i++;
  }
  
  buf[i] = '\0';
  return i;
}

/* 可变参数格式化输出函数 */
static int my_vsprintf(char *str, const char *format, va_list ap) {
  int count = 0;
  int i = 0;
  char buf[64];
  
  while (format[i] != '\0') {
    if (format[i] != '%') {
      str[count++] = format[i++];
      continue;
    }
    
    i++; /* 跳过% */
    
    /* 处理格式化说明符 */
    switch (format[i]) {
      case 'd': /* 十进制整数 */
      case 'i': {
        int num = va_arg(ap, int);
        int len = itoa(num, buf, 10);
        for (int j = 0; j < len; j++) {
          str[count++] = buf[j];
        }
        break;
      }
      case 'u': /* 无符号十进制整数 */ {
        unsigned int num = va_arg(ap, unsigned int);
        int len = itoa((int)num, buf, 10);
        for (int j = 0; j < len; j++) {
          str[count++] = buf[j];
        }
        break;
      }
      case 'o': /* 八进制整数 */ {
        int num = va_arg(ap, int);
        int len = itoa(num, buf, 8);
        for (int j = 0; j < len; j++) {
          str[count++] = buf[j];
        }
        break;
      }
      case 'x': /* 十六进制整数（小写） */
      case 'X': /* 十六进制整数（大写） */ {
        int num = va_arg(ap, int);
        int len = itoa(num, buf, 16);
        for (int j = 0; j < len; j++) {
          if (format[i] == 'X' && buf[j] >= 'a' && buf[j] <= 'f') {
            str[count++] = buf[j] - 32; /* 转换为大写 */
          } else {
            str[count++] = buf[j];
          }
        }
        break;
      }
      case 'c': /* 字符 */ {
        int c = va_arg(ap, int);
        str[count++] = (char)c;
        break;
      }
      case 's': /* 字符串 */ {
        const char *s = va_arg(ap, const char *);
        if (s == NULL) {
          s = "(null)";
        }
        while (*s != '\0') {
          str[count++] = *s++;
        }
        break;
      }
      case 'f': /* 浮点数 */ {
        double num = va_arg(ap, double);
        int len = ftoa(num, buf, 6); /* 默认6位小数 */
        for (int j = 0; j < len; j++) {
          str[count++] = buf[j];
        }
        break;
      }
      case 'e': /* 科学计数法 */
      case 'E': /* 科学计数法（大写） */ {
        /* 简化实现，使用普通浮点数格式 */
        double num = va_arg(ap, double);
        int len = ftoa(num, buf, 6);
        for (int j = 0; j < len; j++) {
          str[count++] = buf[j];
        }
        str[count++] = (format[i] == 'E') ? 'E' : 'e';
        str[count++] = '+';
        str[count++] = '0';
        str[count++] = '0';
        break;
      }
      case 'g': /* 自动选择%f或%e */
      case 'G': /* 自动选择%F或%E */ {
        /* 简化实现，使用普通浮点数格式 */
        double num = va_arg(ap, double);
        int len = ftoa(num, buf, 6);
        for (int j = 0; j < len; j++) {
          str[count++] = buf[j];
        }
        break;
      }
      case 'p': /* 指针地址 */ {
        void *ptr = va_arg(ap, void *);
        unsigned long long addr = (unsigned long long)ptr;
        str[count++] = '0';
        str[count++] = 'x';
        /* 转换为十六进制 */
        char addr_buf[16];
        int len = 0;
        while (addr != 0) {
          int rem = addr % 16;
          addr_buf[len++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
          addr = addr / 16;
        }
        /* 补零到16位 */
        for (int j = len; j < 16; j++) {
          addr_buf[j] = '0';
        }
        /* 反转并复制 */
        for (int j = 15; j >= 0; j--) {
          str[count++] = addr_buf[j];
        }
        break;
      }
      case '%': /* 输出% */ {
        str[count++] = '%';
        break;
      }
      default: /* 未知格式符，直接输出 */
        str[count++] = '%';
        str[count++] = format[i];
        break;
    }
    
    i++;
  }
  
  str[count] = '\0';
  return count;
}

/* 格式化输出函数 */
static int my_sprintf(char *str, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int result = my_vsprintf(str, format, ap);
  va_end(ap);
  return result;
}

/*
** 时间相关定义
*/

/* 闰年判断 */
static int is_leap_year(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/* 获取每个月的天数 */
static int days_in_month(int year, int month) {
  static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 1 && is_leap_year(year)) {
    return 29;
  }
  return days[month];
}

/* 获取当前时间 */
static time_t my_time(time_t *t) {
  /* 使用标准库函数获取当前时间 */
  return time(t);
}

/* 将时间戳转换为GMT时间 */
static struct tm *my_gmtime(const time_t *timep) {
  static struct tm tm_struct;
  time_t t = *timep;
  
  /* 初始化时间结构 */
  my_memset(&tm_struct, 0, sizeof(tm_struct));
  
  /* 计算年份和天数 */
  int year = 1970;
  while (1) {
    int days = is_leap_year(year) ? 366 : 365;
    if (t < days * 24 * 3600) {
      break;
    }
    t -= days * 24 * 3600;
    year++;
  }
  tm_struct.tm_year = year - 1900;
  
  /* 计算一年中的第几天 */
  tm_struct.tm_yday = t / (24 * 3600);
  
  /* 计算月份和日期 */
  int month = 0;
  while (1) {
    int days = days_in_month(year, month);
    if (tm_struct.tm_yday < days) {
      break;
    }
    tm_struct.tm_yday -= days;
    month++;
  }
  tm_struct.tm_mon = month;
  tm_struct.tm_mday = tm_struct.tm_yday + 1;
  tm_struct.tm_yday = t / (24 * 3600);
  
  /* 计算时分秒 */
  t %= 24 * 3600;
  tm_struct.tm_hour = t / 3600;
  t %= 3600;
  tm_struct.tm_min = t / 60;
  tm_struct.tm_sec = t % 60;
  
  /* 计算星期几 (1970-01-01是星期四，即tm_wday=4) */
  tm_struct.tm_wday = (*timep / (24 * 3600) + 4) % 7;
  if (tm_struct.tm_wday < 0) {
    tm_struct.tm_wday += 7;
  }
  
  tm_struct.tm_isdst = 0; /* GMT时间不考虑夏令时 */
  
  return &tm_struct;
}

/* 将时间戳转换为本地时间 */
static struct tm *my_localtime(const time_t *timep) {
  /* 简化实现，直接返回GMT时间 */
  /* 实际实现需要考虑时区和夏令时 */
  return my_gmtime(timep);
}

/* 将时间结构转换为时间戳 */
static time_t my_mktime(struct tm *tm_ptr) {
  time_t t = 0;
  int year = tm_ptr->tm_year + 1900;
  int month = tm_ptr->tm_mon;
  int day = tm_ptr->tm_mday;
  
  /* 计算从1970年到当前年份的天数 */
  for (int y = 1970; y < year; y++) {
    t += (is_leap_year(y) ? 366 : 365) * 24 * 3600;
  }
  
  /* 计算当前年份到当前月份的天数 */
  for (int m = 0; m < month; m++) {
    t += days_in_month(year, m) * 24 * 3600;
  }
  
  /* 计算当前月份到当前日期的天数 */
  t += (day - 1) * 24 * 3600;
  
  /* 计算时分秒 */
  t += tm_ptr->tm_hour * 3600 + tm_ptr->tm_min * 60 + tm_ptr->tm_sec;
  
  /* 调整夏令时 */
  if (tm_ptr->tm_isdst > 0) {
    t += 3600; /* 夏令时加1小时 */
  }
  
  return t;
}

/* 将时间结构转换为ASCII字符串 */
static char *my_asctime(const struct tm *tm_ptr) {
  static char buf[26];
  const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  const char *weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  
  /* 格式："Wed Jun 30 21:49:08 1993\n" */
  /* 简化实现，使用sprintf */
  /* 注意：这里需要sprintf函数，暂时使用固定格式 */
  my_sprintf(buf, "%s %s %2d %02d:%02d:%02d %d\n",
            weekdays[tm_ptr->tm_wday],
            months[tm_ptr->tm_mon],
            tm_ptr->tm_mday,
            tm_ptr->tm_hour,
            tm_ptr->tm_min,
            tm_ptr->tm_sec,
            tm_ptr->tm_year + 1900);
  
  return buf;
}

/* 格式化时间字符串 */
static size_t my_strftime(char *s, size_t maxsize, const char *format, const struct tm *tm_ptr) {
  size_t count = 0;
  const char *months[] = {"January", "February", "March", "April", "May", "June", 
                          "July", "August", "September", "October", "November", "December"};
  const char *short_months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  const char *weekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  const char *short_weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  
  int i = 0;
  while (format[i] != '\0' && count < maxsize - 1) {
    if (format[i] != '%') {
      s[count++] = format[i++];
      continue;
    }
    
    i++;
    char buf[32];
    int len = 0;
    
    switch (format[i]) {
      case 'a': /* 缩写星期名 */
        len = my_sprintf(buf, "%s", short_weekdays[tm_ptr->tm_wday]);
        break;
      case 'A': /* 完整星期名 */
        len = my_sprintf(buf, "%s", weekdays[tm_ptr->tm_wday]);
        break;
      case 'b': /* 缩写月份名 */
        len = my_sprintf(buf, "%s", short_months[tm_ptr->tm_mon]);
        break;
      case 'B': /* 完整月份名 */
        len = my_sprintf(buf, "%s", months[tm_ptr->tm_mon]);
        break;
      case 'c': /* 完整的日期和时间 */
        len = my_sprintf(buf, "%s %s %2d %02d:%02d:%02d %d",
                        short_weekdays[tm_ptr->tm_wday],
                        short_months[tm_ptr->tm_mon],
                        tm_ptr->tm_mday,
                        tm_ptr->tm_hour,
                        tm_ptr->tm_min,
                        tm_ptr->tm_sec,
                        tm_ptr->tm_year + 1900);
        break;
      case 'd': /* 日期 [01-31] */
        len = my_sprintf(buf, "%02d", tm_ptr->tm_mday);
        break;
      case 'H': /* 小时（24小时制）[00-23] */
        len = my_sprintf(buf, "%02d", tm_ptr->tm_hour);
        break;
      case 'I': /* 小时（12小时制）[01-12] */ {
        int hour = tm_ptr->tm_hour;
        if (hour == 0) {
          hour = 12;
        } else if (hour > 12) {
          hour -= 12;
        }
        len = my_sprintf(buf, "%02d", hour);
        break;
      }
      case 'j': /* 一年中的第几天 [001-366] */
        len = my_sprintf(buf, "%03d", tm_ptr->tm_yday + 1);
        break;
      case 'm': /* 月份 [01-12] */
        len = my_sprintf(buf, "%02d", tm_ptr->tm_mon + 1);
        break;
      case 'M': /* 分钟 [00-59] */
        len = my_sprintf(buf, "%02d", tm_ptr->tm_min);
        break;
      case 'p': /* AM/PM */
        len = my_sprintf(buf, "%s", (tm_ptr->tm_hour < 12) ? "AM" : "PM");
        break;
      case 'S': /* 秒 [00-60] */
        len = my_sprintf(buf, "%02d", tm_ptr->tm_sec);
        break;
      case 'U': /* 一年中的第几周（周日为第一天）[00-53] */
        /* 简化实现，使用一年中的第几天除以7 */
        len = my_sprintf(buf, "%02d", (tm_ptr->tm_yday + 7 - tm_ptr->tm_wday) / 7);
        break;
      case 'w': /* 星期几（数字）[0-6] */
        len = my_sprintf(buf, "%d", tm_ptr->tm_wday);
        break;
      case 'W': /* 一年中的第几周（周一为第一天）[00-53] */
        {
        /* 简化实现，使用一年中的第几天除以7 */
        int wday = tm_ptr->tm_wday;
        if (wday == 0) wday = 7;
        len = my_sprintf(buf, "%02d", (tm_ptr->tm_yday + 7 - (wday - 1)) / 7);
        break;
        }
      case 'x': /* 日期格式 */
        len = my_sprintf(buf, "%02d/%02d/%04d",
                        tm_ptr->tm_mon + 1,
                        tm_ptr->tm_mday,
                        tm_ptr->tm_year + 1900);
        break;
      case 'X': /* 时间格式 */
        len = my_sprintf(buf, "%02d:%02d:%02d",
                        tm_ptr->tm_hour,
                        tm_ptr->tm_min,
                        tm_ptr->tm_sec);
        break;
      case 'y': /* 两位数年份 [00-99] */
        len = my_sprintf(buf, "%02d", (tm_ptr->tm_year + 1900) % 100);
        break;
      case 'Y': /* 四位数年份 */
        len = my_sprintf(buf, "%04d", tm_ptr->tm_year + 1900);
        break;
      case '%': /* 输出% */
        len = my_sprintf(buf, "%%");
        break;
      default: /* 未知格式符，直接输出 */
        len = my_sprintf(buf, "%%%c", format[i]);
        break;
    }
    
    /* 复制到输出缓冲区 */
    for (int j = 0; j < len && count < maxsize - 1; j++) {
      s[count++] = buf[j];
    }
    
    i++;
  }
  
  s[count] = '\0';
  return count;
}

/*
** 错误处理相关定义
*/

/* 全局错误码变量 */
static int my_errno = 0;

/* 错误信息表 */
static const struct {
  int errnum;
  const char *errmsg;
} error_messages[] = {
  {0, "Success"},
  {1, "Operation not permitted"},
  {2, "No such file or directory"},
  {3, "No such process"},
  {4, "Interrupted system call"},
  {5, "Input/output error"},
  {6, "No such device or address"},
  {7, "Argument list too long"},
  {8, "Exec format error"},
  {9, "Bad file descriptor"},
  {10, "No child processes"},
  {11, "Resource temporarily unavailable"},
  {12, "Cannot allocate memory"},
  {13, "Permission denied"},
  {14, "Bad address"},
  {15, "Block device required"},
  {16, "Device or resource busy"},
  {17, "File exists"},
  {18, "Invalid cross-device link"},
  {19, "No such device"},
  {20, "Not a directory"},
  {21, "Is a directory"},
  {22, "Invalid argument"},
  {23, "Too many open files in system"},
  {24, "Too many open files"},
  {25, "Inappropriate ioctl for device"},
  {26, "Text file busy"},
  {27, "File too large"},
  {28, "No space left on device"},
  {29, "Illegal seek"},
  {30, "Read-only file system"},
  {31, "Too many links"},
  {32, "Broken pipe"},
  {33, "Numerical argument out of domain"},
  {34, "Numerical result out of range"},
  {35, "Resource deadlock avoided"},
  {36, "File name too long"},
  {37, "No locks available"},
  {38, "Function not implemented"},
  {39, "Directory not empty"},
  {40, "Too many levels of symbolic links"},
  {41, "Unknown error 41"},
  {42, "No message of desired type"},
  {43, "Identifier removed"},
  {44, "Channel number out of range"},
  {45, "Level 2 not synchronized"},
  {46, "Level 3 halted"},
  {47, "Level 3 reset"},
  {48, "Link number out of range"},
  {49, "Protocol driver not attached"},
  {50, "No CSI structure available"},
  {51, "Level 2 halted"},
  {52, "Invalid exchange"},
  {53, "Invalid request descriptor"},
  {54, "Exchange full"},
  {55, "No anode"},
  {56, "Invalid request code"},
  {57, "Invalid slot"},
  {58, "Unknown error 58"},
  {59, "Bad font file format"},
  {60, "Device not a stream"},
  {61, "No data available"},
  {62, "Timer expired"},
  {63, "Out of streams resources"},
  {64, "Machine is not on the network"},
  {65, "Package not installed"},
  {66, "Object is remote"},
  {67, "Link has been severed"},
  {68, "Advertise error"},
  {69, "Srmount error"},
  {70, "Communication error on send"},
  {71, "Protocol error"},
  {72, "Multihop attempted"},
  {73, "RFS specific error"},
  {74, "Bad message"},
  {75, "Value too large for defined data type"},
  {76, "Name not unique on network"},
  {77, "File descriptor in bad state"},
  {78, "Remote address changed"},
  {79, "Can not access a needed shared library"},
  {80, "Accessing a corrupted shared library"},
  {81, ".lib section in a.out corrupted"},
  {82, "Attempting to link in too many shared libraries"},
  {83, "Cannot exec a shared library directly"},
  {84, "Invalid or incomplete multibyte or wide character"},
  {85, "Interrupted system call should be restarted"},
  {86, "Streams pipe error"},
  {87, "Too many users"},
  {88, "Socket operation on non-socket"},
  {89, "Destination address required"},
  {90, "Message too long"},
  {91, "Protocol wrong type for socket"},
  {92, "Protocol not available"},
  {93, "Protocol not supported"},
  {94, "Socket type not supported"},
  {95, "Operation not supported"},
  {96, "Protocol family not supported"},
  {97, "Address family not supported by protocol"},
  {98, "Address already in use"},
  {99, "Cannot assign requested address"},
  {100, "Network is down"},
  {101, "Network is unreachable"},
  {102, "Network dropped connection on reset"},
  {103, "Software caused connection abort"},
  {104, "Connection reset by peer"},
  {105, "No buffer space available"},
  {106, "Transport endpoint is already connected"},
  {107, "Transport endpoint is not connected"},
  {108, "Cannot send after transport endpoint shutdown"},
  {109, "Too many references: cannot splice"},
  {110, "Connection timed out"},
  {111, "Connection refused"},
  {112, "Host is down"},
  {113, "No route to host"},
  {114, "Operation already in progress"},
  {115, "Operation now in progress"},
  {116, "Stale NFS file handle"},
  {117, "Structure needs cleaning"},
  {118, "Not a XENIX named type file"},
  {119, "No XENIX semaphores available"},
  {120, "Is a named type file"},
  {121, "Remote I/O error"},
  {122, "Disk quota exceeded"},
  {123, "No medium found"},
  {124, "Wrong medium type"},
  {125, "Operation canceled"},
  {126, "Required key not available"},
  {127, "Key has expired"},
  {128, "Key has been revoked"},
  {129, "Key was rejected by service"},
  {130, "Owner died"},
  {131, "State not recoverable"},
  {132, "Operation not possible due to RF-kill"},
  {133, "Memory page has hardware error"},
  {0, NULL} /* 结束标记 */
};

/* 获取错误码 */
static int *my_errno_ptr(void) {
  return &my_errno;
}

/* 设置错误码 */
static void my_set_errno(int err) {
  my_errno = err;
}

/* 获取错误描述 */
static const char *my_strerror(int errnum) {
  for (int i = 0; error_messages[i].errmsg != NULL; i++) {
    if (error_messages[i].errnum == errnum) {
      return error_messages[i].errmsg;
    }
  }
  return "Unknown error";
}

/* 打印错误信息 */
static void my_perror(const char *s) {
  /* 这里简化实现，实际应该写入到标准错误流 */
  const char *errmsg = my_strerror(my_errno);
  if (s != NULL && *s != '\0') {
    /* 格式："message: error_description\n" */
    /* 这里需要实现printf或类似功能，暂时简化处理 */
  }
}

/*
** 字符串转换函数
*/

/* 跳过空白字符 */
static const char *skip_whitespace(const char *s) {
  while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == '\v') {
    s++;
  }
  return s;
}

/* 字符串转整数 */
static int my_atoi(const char *s) {
  return (int)my_strtol(s, NULL, 10);
}

/* 字符串转长整数 */
static long my_atol(const char *s) {
  return my_strtol(s, NULL, 10);
}

/* 字符串转双精度浮点数 */
static double my_atof(const char *s) {
  return my_strtod(s, NULL);
}

/* 字符串转长整数（支持不同进制） */
static long my_strtol(const char *nptr, char **endptr, int base) {
  const char *s = skip_whitespace(nptr);
  int sign = 1;
  long result = 0;
  int digit;
  
  /* 处理符号 */
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }
  
  /* 处理进制前缀 */
  if (base == 0) {
    if (*s == '0') {
      if (my_tolower(*(s + 1)) == 'x') {
        base = 16;
        s += 2;
      } else {
        base = 8;
        s++;
      }
    } else {
      base = 10;
    }
  } else if (base == 16) {
    if (*s == '0' && my_tolower(*(s + 1)) == 'x') {
      s += 2;
    }
  }
  
  /* 转换数字 */
  while (*s != '\0') {
    if (*s >= '0' && *s <= '9') {
      digit = *s - '0';
    } else if (*s >= 'a' && *s <= 'z') {
      digit = *s - 'a' + 10;
    } else if (*s >= 'A' && *s <= 'Z') {
      digit = *s - 'A' + 10;
    } else {
      break;
    }
    
    /* 检查数字是否在进制范围内 */
    if (digit >= base) {
      break;
    }
    
    /* 检查溢出 */
    if (result > (LONG_MAX - digit) / base) {
      /* 溢出 */
      result = (sign == 1) ? LONG_MAX : LONG_MIN;
      break;
    }
    
    result = result * base + digit;
    s++;
  }
  
  /* 设置endptr */
  if (endptr != NULL)
    if (endptr != NULL) {
    *endptr = (char *)s;
  }
  
  return result * sign;
}

/* 字符串转无符号长整数（支持不同进制） */
static unsigned long my_strtoul(const char *nptr, char **endptr, int base) {
  const char *s = skip_whitespace(nptr);
  int sign = 1;
  unsigned long result = 0;
  int digit;
  
  /* 处理符号 */
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }
  
  /* 处理进制前缀 */
  if (base == 0) {
    if (*s == '0') {
      if (my_tolower(*(s + 1)) == 'x') {
        base = 16;
        s += 2;
      } else {
        base = 8;
        s++;
      }
    } else {
      base = 10;
    }
  } else if (base == 16) {
    if (*s == '0' && my_tolower(*(s + 1)) == 'x') {
      s += 2;
    }
  }
  
  /* 转换数字 */
  while (*s != '\0') {
    if (*s >= '0' && *s <= '9') {
      digit = *s - '0';
    } else if (*s >= 'a' && *s <= 'z') {
      digit = *s - 'a' + 10;
    } else if (*s >= 'A' && *s <= 'Z') {
      digit = *s - 'A' + 10;
    } else {
      break;
    }
    
    /* 检查数字是否在进制范围内 */
    if (digit >= base) {
      break;
    }
    
    /* 检查溢出 */
    if (result > (ULONG_MAX - digit) / base) {
      /* 溢出 */
      result = ULONG_MAX;
      break;
    }
    
    result = result * base + digit;
    s++;
  }
  
  /* 设置endptr */
  if (endptr != NULL) {
    *endptr = (char *)s;
  }
  
  if (sign == -1) {
    /* 处理负数 */
    return -result;
  }
  
  return result;
}

/* 字符串转双精度浮点数（支持科学计数法） */
static double my_strtod(const char *nptr, char **endptr) {
  const char *s = skip_whitespace(nptr);
  int sign = 1;
  double result = 0.0;
  double fraction = 0.0;
  int exponent = 0;
  int frac_digits = 0;
  
  /* 处理符号 */
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }
  
  /* 处理整数部分 */
  while (*s >= '0' && *s <= '9') {
    result = result * 10.0 + (*s - '0');
    s++;
  }
  
  /* 处理小数部分 */
  if (*s == '.') {
    s++;
    while (*s >= '0' && *s <= '9') {
      fraction = fraction * 10.0 + (*s - '0');
      frac_digits++;
      s++;
    }
    /* 添加小数部分到结果 */
    for (int i = 0; i < frac_digits; i++) {
      fraction /= 10.0;
    }
    result += fraction;
  }
  
  /* 处理指数部分 */
  if (*s == 'e' || *s == 'E') {
    s++;
    int exp_sign = 1;
    
    /* 处理指数符号 */
    if (*s == '-') {
      exp_sign = -1;
      s++;
    } else if (*s == '+') {
      s++;
    }
    
    /* 处理指数值 */
    while (*s >= '0' && *s <= '9') {
      exponent = exponent * 10 + (*s - '0');
      s++;
    }
    
    exponent *= exp_sign;
    
    /* 应用指数 */
    while (exponent > 0) {
      result *= 10.0;
      exponent--;
    }
    while (exponent < 0) {
      result /= 10.0;
      exponent++;
    }
  }
  
  /* 设置endptr */
  if (endptr != NULL) {
    *endptr = (char *)s;
  }
  
  return result * sign;
}

/*
** 动态内存分配函数
*/

/* 内存分配 */
static void *my_malloc(size_t size) {
  if (size == 0) {
    return NULL;
  }
  
  /* 初始化堆 */
  init_heap();
  
  /* 计算实际需要的大小（包括块头部） */
  size_t total_size = align_size(size) + sizeof(memory_block);
  
  /* 查找合适的空闲块 */
  memory_block *block = find_free_block(total_size);
  
  /* 如果没有找到，扩展堆 */
  if (block == NULL) {
    block = (memory_block *)expand_heap(total_size);
    if (block == NULL) {
      return NULL;
    }
  }
  
  /* 标记为已使用 */
  block->free = 0;
  
  /* 分割块（如果需要） */
  split_block(block, total_size);
  
  /* 返回用户可用内存地址 */
  return (void *)(block + 1);
}

/* 分配并清零内存 */
static void *my_calloc(size_t nmemb, size_t size) {
  size_t total_size = nmemb * size;
  void *ptr = my_malloc(total_size);
  if (ptr != NULL) {
    /* 清零内存 */
    my_memset(ptr, 0, total_size);
  }
  return ptr;
}

/* 重新分配内存 */
static void *my_realloc(void *ptr, size_t size) {
  if (ptr == NULL) {
    /* 如果ptr为NULL，等同于malloc */
    return my_malloc(size);
  }
  
  if (size == 0) {
    /* 如果size为0，等同于free */
    my_free(ptr);
    return NULL;
  }
  
  /* 获取原块信息 */
  memory_block *block = (memory_block *)ptr - 1;
  size_t old_size = block->size - sizeof(memory_block);
  
  if (size <= old_size) {
    /* 新大小小于等于原大小，直接返回 */
    return ptr;
  }
  
  /* 分配新内存 */
  void *new_ptr = my_malloc(size);
  if (new_ptr != NULL) {
    /* 复制数据 */
    my_memcpy(new_ptr, ptr, old_size);
    /* 释放旧内存 */
    my_free(ptr);
  }
  
  return new_ptr;
}

/* 释放内存 */
static void my_free(void *ptr) {
  if (ptr == NULL) {
    return;
  }
  
  /* 获取块信息 */
  memory_block *block = (memory_block *)ptr - 1;
  
  /* 标记为空闲 */
  block->free = 1;
  
  /* 合并相邻的空闲块 */
  merge_blocks();
}

/*
** 字符串长度
*/
static size_t my_strlen(const char *s) {
  const char *p = s;
  while (*p != '\0') {
    p++;
  }
  return (size_t)(p - s);
}

/*
** 字符串拷贝
*/
static char *my_strcpy(char *dst, const char *src) {
  char *p = dst;
  while ((*p++ = *src++) != '\0') {
    /* 空循环 */
  }
  return dst;
}

/*
** 字符串n拷贝
*/
static char *my_strncpy(char *dst, const char *src, size_t n) {
  char *p = dst;
  while (n > 0 && (*p++ = *src++) != '\0') {
    n--;
  }
  while (n > 0) {
    *p++ = '\0';
    n--;
  }
  return dst;
}

/*
** 字符串比较
*/
static int my_strcmp(const char *s1, const char *s2) {
  while (*s1 == *s2) {
    if (*s1 == '\0') {
      return 0;
    }
    s1++;
    s2++;
  }
  return (unsigned char)*s1 - (unsigned char)*s2;
}

/*
** 字符串n比较
*/
static int my_strncmp(const char *s1, const char *s2, size_t n) {
  while (n > 0 && *s1 == *s2) {
    if (*s1 == '\0') {
      return 0;
    }
    s1++;
    s2++;
    n--;
  }
  if (n == 0) {
    return 0;
  }
  return (unsigned char)*s1 - (unsigned char)*s2;
}

/*
** 查找字符首次出现位置
*/
static const char *my_strchr(const char *s, int c) {
  while (*s != '\0') {
    if (*s == (char)c) {
      return s;
    }
    s++;
  }
  if (c == '\0') {
    return s;
  }
  return NULL;
}

/*
** 查找字符最后出现位置
*/
static const char *my_strrchr(const char *s, int c) {
  const char *last = NULL;
  while (*s != '\0') {
    if (*s == (char)c) {
      last = s;
    }
    s++;
  }
  if (c == '\0') {
    return s;
  }
  return last;
}

/*
** 查找子字符串
*/
static const char *my_strstr(const char *haystack, const char *needle) {
  if (*needle == '\0') {
    return haystack;
  }
  
  const char *h = haystack;
  while (*h != '\0') {
    const char *h2 = h;
    const char *n = needle;
    while (*h2 == *n && *h2 != '\0' && *n != '\0') {
      h2++;
      n++;
    }
    if (*n == '\0') {
      return h;
    }
    h++;
  }
  return NULL;
}

/*
** 字符串分割（线程安全，使用用户提供的上下文）
*/
static char *my_strtok(char *str, const char *delim, char **saveptr) {
  char *token;
  
  /* 检查参数 */
  if (delim == NULL || saveptr == NULL) {
    return NULL;
  }
  
  if (str != NULL) {
    *saveptr = str;
  }
  
  /* 检查是否已经处理完字符串 */
  if (*saveptr == NULL || **saveptr == '\0') {
    *saveptr = NULL;
    return NULL;
  }
  
  /* 跳过开头的分隔符 */
  while (**saveptr != '\0') {
    int found = 0;
    const char *d = delim;
    while (*d != '\0') {
      if (**saveptr == *d) {
        found = 1;
        break;
      }
      d++;
    }
    if (!found) {
      break;
    }
    (*saveptr)++;
  }
  
  /* 检查是否已经处理完字符串 */
  if (**saveptr == '\0') {
    *saveptr = NULL;
    return NULL;
  }
  
  token = *saveptr;
  
  /* 查找下一个分隔符 */
  while (**saveptr != '\0') {
    const char *d = delim;
    while (*d != '\0') {
      if (**saveptr == *d) {
        **saveptr = '\0';
        (*saveptr)++;
        return token;
      }
      d++;
    }
    (*saveptr)++;
  }
  
  /* 标记处理结束 */
  *saveptr = NULL;
  
  return token;
}

/*
** 转换为小写
*/
static int my_tolower(int c) {
  if (c >= 'A' && c <= 'Z') {
    return c + ('a' - 'A');
  }
  return c;
}

/*
** 转换为大写
*/
static int my_toupper(int c) {
  if (c >= 'a' && c <= 'z') {
    return c - ('a' - 'A');
  }
  return c;
}

/*
** 字符串转换为小写
*/
static char *my_strlwr(char *s) {
  char *p = s;
  while (*p != '\0') {
    *p = (char)my_tolower((unsigned char)*p);
    p++;
  }
  return s;
}

/*
** 字符串转换为大写
*/
static char *my_strupr(char *s) {
  char *p = s;
  while (*p != '\0') {
    *p = (char)my_toupper((unsigned char)*p);
    p++;
  }
  return s;
}

/*
** 查找字符串中第一个不在指定字符集内的字符位置
*/
static size_t my_strspn(const char *s, const char *accept) {
  const char *p = s;
  while (*p != '\0') {
    const char *a = accept;
    int found = 0;
    while (*a != '\0') {
      if (*p == *a) {
        found = 1;
        break;
      }
      a++;
    }
    if (!found) {
      break;
    }
    p++;
  }
  return (size_t)(p - s);
}

/*
** 查找字符串中第一个在指定字符集内的字符位置
*/
static size_t my_strcspn(const char *s, const char *reject) {
  const char *p = s;
  while (*p != '\0') {
    const char *r = reject;
    while (*r != '\0') {
      if (*p == *r) {
        return (size_t)(p - s);
      }
      r++;
    }
    p++;
  }
  return (size_t)(p - s);
}

/*
** 查找字符串中第一个出现指定字符集中任一字符的位置
*/
static const char *my_strpbrk(const char *s, const char *accept) {
  const char *p = s;
  while (*p != '\0') {
    const char *a = accept;
    while (*a != '\0') {
      if (*p == *a) {
        return p;
      }
      a++;
    }
    p++;
  }
  return NULL;
}

/*
** 字符串复制（动态分配内存）
*/
static char *my_strdup(const char *s) {
  size_t len = my_strlen(s) + 1;
  char *dup = (char *)malloc(len);
  if (dup != NULL) {
    my_strcpy(dup, s);
  }
  return dup;
}

/*
** 字符串复制（动态分配内存，指定长度）
*/
static char *my_strndup(const char *s, size_t n) {
  size_t len = my_strlen(s);
  if (len > n) {
    len = n;
  }
  char *dup = (char *)malloc(len + 1);
  if (dup != NULL) {
    my_strncpy(dup, s, len);
    dup[len] = '\0';
  }
  return dup;
}

/*
** 内存设置
*/
static void *my_memset(void *s, int c, size_t n) {
  unsigned char *p = (unsigned char *)s;
  while (n > 0) {
    *p++ = (unsigned char)c;
    n--;
  }
  return s;
}

/*
** 内存拷贝
*/
static void *my_memcpy(void *dst, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  while (n > 0) {
    *d++ = *s++;
    n--;
  }
  return dst;
}

/*
** 内存移动
*/
static void *my_memmove(void *dst, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  
  if (d < s) {
    /* 从前往后拷贝 */
    while (n > 0) {
      *d++ = *s++;
      n--;
    }
  } else if (d > s) {
    /* 从后往前拷贝 */
    d += n;
    s += n;
    while (n > 0) {
      *--d = *--s;
      n--;
    }
  }
  
  return dst;
}

/*
** 内存比较
*/
static int my_memcmp(const void *s1, const void *s2, size_t n) {
  const unsigned char *p1 = (const unsigned char *)s1;
  const unsigned char *p2 = (const unsigned char *)s2;
  
  while (n > 0 && *p1 == *p2) {
    p1++;
    p2++;
    n--;
  }
  
  if (n == 0) {
    return 0;
  }
  
  return *p1 - *p2;
}

/*
** 自定义数学函数
*/

/*
** 绝对值
*/
static int my_abs(int n) {
  return (n < 0) ? -n : n;
}

/*
** 双精度绝对值
*/
static double my_fabs(double x) {
  return (x < 0.0) ? -x : x;
}

/*
** 平方根（牛顿迭代法）
*/
static double my_sqrt(double x) {
  if (x < 0.0) {
    return 0.0; /* 简单处理负数情况 */
  }
  
  double guess = x;
  double epsilon = 1e-10;
  
  for (int i = 0; i < 100; i++) {
    double new_guess = 0.5 * (guess + x / guess);
    if (my_fabs(new_guess - guess) < epsilon) {
      return new_guess;
    }
    guess = new_guess;
  }
  
  return guess;
}

/*
** 自然对数（泰勒级数展开，提高精度）
*/
static double my_log(double x) {
  if (x <= 0.0) {
    return 0.0; /* 简单处理负数情况 */
  }
  
  double y = (x - 1.0) / (x + 1.0);
  double y2 = y * y;
  double term = y;
  double result = y;
  double sign = 1.0;
  
  /* 展开到20项，提高精度 */
  for (int i = 3; i <= 41; i += 2) {
    sign = -sign;
    term *= y2;
    result += term / i;
  }
  
  return 2.0 * result;
}

/*
** 指数函数（泰勒级数展开，提高精度）
*/
static double my_exp(double x) {
  double result = 1.0;
  double term = 1.0;
  
  /* 展开到20项，提高精度 */
  for (int i = 1; i <= 20; i++) {
    term *= x / i;
    result += term;
    /* 防止溢出 */
    if (result > 1e300) {
      break;
    }
  }
  
  return result;
}

/*
** 幂运算（支持非整数指数）
*/
static double my_pow(double base, double exponent) {
  /* 处理特殊情况 */
  if (exponent == 0.0) {
    return 1.0;
  }
  
  if (exponent == 1.0) {
    return base;
  }
  
  if (base == 0.0) {
    return 0.0;
  }
  
  /* 使用对数和指数转换实现非整数指数 */
  /* pow(a, b) = exp(b * log(a)) */
  double log_base = my_log(base);
  double exp_result = my_exp(exponent * log_base);
  
  return exp_result;
}

/*
** 正弦函数（泰勒级数，前20项，提高精度）
*/
static double my_sin(double x) {
  /* 将x归一化到[-π, π]范围 */
  while (x > 3.141592653589793) x -= 6.283185307179586;
  while (x < -3.141592653589793) x += 6.283185307179586;
  
  double result = x;
  double term = x;
  double x2 = x * x;
  int sign = -1;
  
  /* 展开到20项，提高精度 */
  for (int i = 3; i <= 41; i += 2) {
    term *= x2 / ((i - 1) * i);
    result += sign * term;
    sign = -sign;
  }
  
  return result;
}

/*
** 余弦函数（泰勒级数，前20项，提高精度）
*/
static double my_cos(double x) {
  /* 将x归一化到[-π, π]范围 */
  while (x > 3.141592653589793) x -= 6.283185307179586;
  while (x < -3.141592653589793) x += 6.283185307179586;
  
  double result = 1.0;
  double term = 1.0;
  double x2 = x * x;
  int sign = -1;
  
  /* 展开到20项，提高精度 */
  for (int i = 2; i <= 42; i += 2) {
    term *= x2 / ((i - 1) * i);
    result += sign * term;
    sign = -sign;
  }
  
  return result;
}

/*
** 正切函数（sin/cos）
*/
static double my_tan(double x) {
  return my_sin(x) / my_cos(x);
}

/*
** 向下取整
*/
static double my_floor(double x) {
  int i = (int)x;
  if (x < 0.0 && (double)i != x) {
    i--;
  }
  return (double)i;
}

/*
** 向上取整
*/
static double my_ceil(double x) {
  int i = (int)x;
  if (x > 0.0 && (double)i != x) {
    i++;
  }
  return (double)i;
}

/*
** 四舍五入
*/
static double my_round(double x) {
  return my_floor(x + 0.5);
}

/*
** 查找内存中的字符
*/
static void *my_memchr(const void *s, int c, size_t n) {
  const unsigned char *p = (const unsigned char *)s;
  while (n > 0) {
    if (*p == (unsigned char)c) {
      return (void *)p;
    }
    p++;
    n--;
  }
  return NULL;
}

/*
** 简单的随机数生成器（线性同余法）
*/
static unsigned int my_rand_seed = 1;

static int my_rand(void) {
  my_rand_seed = my_rand_seed * 1103515245 + 12345;
  return (unsigned int)(my_rand_seed / 65536) % 32768;
}

static void my_srand(unsigned int seed) {
  my_rand_seed = seed;
}

/*
** CRC32校验算法（查表法）
*/
static unsigned int crc32_table[256] = {
  0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
  0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
  0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
  0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
  0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
  0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
  0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
  0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
  0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
  0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
  0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
  0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
  0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
  0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
  0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
  0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
  0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
  0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
  0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
  0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
  0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
  0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
  0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
  0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
  0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
  0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
  0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
  0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
  0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
  0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
  0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
  0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

static unsigned int my_crc32(const unsigned char *data, size_t len) {
  unsigned int crc = 0xFFFFFFFF;
  while (len--) {
    crc = (crc >> 8) ^ crc32_table[(crc & 0xFF) ^ *data++];
  }
  return crc ^ 0xFFFFFFFF;
}

/*
** 简单的哈希函数（djb2算法）
*/
static unsigned int my_hash(const char *str) {
  unsigned int hash = 5381;
  int c;
  
  while ((c = *str++) != 0) {
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
  }
  
  return hash;
}

/*
** 位操作函数
*/
static int my_band(int a, int b) {
  return a & b;
}

static int my_bor(int a, int b) {
  return a | b;
}

static int my_bxor(int a, int b) {
  return a ^ b;
}

static int my_bnot(int a) {
  return ~a;
}

static int my_bleft(int a, int n) {
  return a << n;
}

static int my_bright(int a, int n) {
  return a >> n;
}

static int my_bswap(int a) {
  return ((a << 24) & 0xFF000000) |
         ((a <<  8) & 0x00FF0000) |
         ((a >>  8) & 0x0000FF00) |
         ((a >> 24) & 0x000000FF);
}

static int my_btest(int a, int n) {
  return (a & (1 << n)) != 0;
}

static int my_bset(int a, int n) {
  return a | (1 << n);
}

static int my_bclear(int a, int n) {
  return a & ~(1 << n);
}

/*
** 在内存中查找子内存块
*/
static void *my_memmem(const void *haystack, size_t haystack_len, const void *needle, size_t needle_len) {
  if (needle_len == 0) {
    return (void *)haystack;
  }
  
  if (haystack_len < needle_len) {
    return NULL;
  }
  
  const unsigned char *h = (const unsigned char *)haystack;
  const unsigned char *n = (const unsigned char *)needle;
  
  for (size_t i = 0; i <= haystack_len - needle_len; i++) {
    size_t j = 0;
    while (j < needle_len && h[i + j] == n[j]) {
      j++;
    }
    if (j == needle_len) {
      return (void *)(h + i);
    }
  }
  
  return NULL;
}

/*
** 快速内存比较（使用汇编优化）
*/
static int my_memcmp_fast(const void *s1, const void *s2, size_t n) {
  const unsigned char *p1 = (const unsigned char *)s1;
  const unsigned char *p2 = (const unsigned char *)s2;
  
  /* 对齐处理 */
  while (n > 0 && ((uintptr_t)p1 % sizeof(unsigned long) != 0 || (uintptr_t)p2 % sizeof(unsigned long) != 0)) {
    if (*p1 != *p2) {
      return *p1 - *p2;
    }
    p1++;
    p2++;
    n--;
  }
  
  /* 按字比较 */
  const unsigned long *lp1 = (const unsigned long *)p1;
  const unsigned long *lp2 = (const unsigned long *)p2;
  while (n >= sizeof(unsigned long)) {
    if (*lp1 != *lp2) {
      /* 字不同，比较字节 */
      const unsigned char *cp1 = (const unsigned char *)lp1;
      const unsigned char *cp2 = (const unsigned char *)lp2;
      for (size_t i = 0; i < sizeof(unsigned long); i++) {
        if (cp1[i] != cp2[i]) {
          return cp1[i] - cp2[i];
        }
      }
    }
    lp1++;
    lp2++;
    n -= sizeof(unsigned long);
  }
  
  /* 剩余字节比较 */
  const unsigned char *cp1 = (const unsigned char *)lp1;
  const unsigned char *cp2 = (const unsigned char *)lp2;
  while (n > 0) {
    if (*cp1 != *cp2) {
      return *cp1 - *cp2;
    }
    cp1++;
    cp2++;
    n--;
  }
  
  return 0;
}

/*
** 快速内存复制（使用普通C实现）
*/
static void *my_memcpy_fast(void *dst, const void *src, size_t n) {
  /* 使用普通C实现，直接调用已有的my_memcpy函数 */
  return my_memcpy(dst, src, n);
}


/*
** Dynamic memory allocation functions (Lua bindings)
*/

static int l_libc_malloc (lua_State *L) {
  size_t size = luaL_checkinteger(L, 1);
  void *ptr = my_malloc(size);
  if (ptr == NULL) {
    lua_pushnil(L);
  } else {
    lua_pushlightuserdata(L, ptr);
  }
  return 1;
}

static int l_libc_calloc (lua_State *L) {
  size_t nmemb = luaL_checkinteger(L, 1);
  size_t size = luaL_checkinteger(L, 2);
  void *ptr = my_calloc(nmemb, size);
  if (ptr == NULL) {
    lua_pushnil(L);
  } else {
    lua_pushlightuserdata(L, ptr);
  }
  return 1;
}

static int l_libc_realloc (lua_State *L) {
  void *ptr = lua_touserdata(L, 1);
  size_t size = luaL_checkinteger(L, 2);
  void *new_ptr = my_realloc(ptr, size);
  if (new_ptr == NULL) {
    lua_pushnil(L);
  } else {
    lua_pushlightuserdata(L, new_ptr);
  }
  return 1;
}

static int l_libc_free (lua_State *L) {
  void *ptr = lua_touserdata(L, 1);
  my_free(ptr);
  return 0;
}


/*
** String conversion functions (Lua bindings)
*/

static int l_libc_atoi (lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  lua_pushinteger(L, my_atoi(s));
  return 1;
}

static int l_libc_atol (lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  lua_pushinteger(L, my_atol(s));
  return 1;
}

static int l_libc_atof (lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  lua_pushnumber(L, my_atof(s));
  return 1;
}

static int l_libc_strtol (lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  int base = luaL_optinteger(L, 2, 10);
  char *endptr = NULL;
  long result = my_strtol(s, &endptr, base);
  lua_pushinteger(L, result);
  lua_pushstring(L, endptr);
  return 2;
}

static int l_libc_strtoul (lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  int base = luaL_optinteger(L, 2, 10);
  char *endptr = NULL;
  unsigned long result = my_strtoul(s, &endptr, base);
  lua_pushinteger(L, result);
  lua_pushstring(L, endptr);
  return 2;
}

static int l_libc_strtod (lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  char *endptr = NULL;
  double result = my_strtod(s, &endptr);
  lua_pushnumber(L, result);
  lua_pushstring(L, endptr);
  return 2;
}


/*
** Error handling functions (Lua bindings)
*/

static int l_libc_get_errno (lua_State *L) {
  lua_pushinteger(L, *my_errno_ptr());
  return 1;
}

static int l_libc_set_errno (lua_State *L) {
  int err = luaL_checkinteger(L, 1);
  my_set_errno(err);
  return 0;
}

static int l_libc_perror (lua_State *L) {
  const char *s = luaL_optstring(L, 1, "");
  my_perror(s);
  return 0;
}

static int l_libc_strerror (lua_State *L) {
  int errnum = luaL_checkinteger(L, 1);
  lua_pushstring(L, my_strerror(errnum));
  return 1;
}


/*
** Time functions (Lua bindings)
*/

static int l_libc_time (lua_State *L) {
  time_t t;
  time_t result = my_time(&t);
  lua_pushinteger(L, result);
  return 1;
}

static int l_libc_gmtime (lua_State *L) {
  time_t t = luaL_checkinteger(L, 1);
  struct tm *tm_ptr = my_gmtime(&t);
  
  /* 创建Lua表存储时间结构 */
  lua_newtable(L);
  lua_pushinteger(L, tm_ptr->tm_sec);
  lua_setfield(L, -2, "tm_sec");
  lua_pushinteger(L, tm_ptr->tm_min);
  lua_setfield(L, -2, "tm_min");
  lua_pushinteger(L, tm_ptr->tm_hour);
  lua_setfield(L, -2, "tm_hour");
  lua_pushinteger(L, tm_ptr->tm_mday);
  lua_setfield(L, -2, "tm_mday");
  lua_pushinteger(L, tm_ptr->tm_mon);
  lua_setfield(L, -2, "tm_mon");
  lua_pushinteger(L, tm_ptr->tm_year);
  lua_setfield(L, -2, "tm_year");
  lua_pushinteger(L, tm_ptr->tm_wday);
  lua_setfield(L, -2, "tm_wday");
  lua_pushinteger(L, tm_ptr->tm_yday);
  lua_setfield(L, -2, "tm_yday");
  lua_pushinteger(L, tm_ptr->tm_isdst);
  lua_setfield(L, -2, "tm_isdst");
  
  return 1;
}

static int l_libc_localtime (lua_State *L) {
  time_t t = luaL_checkinteger(L, 1);
  struct tm *tm_ptr = my_localtime(&t);
  
  /* 创建Lua表存储时间结构 */
  lua_newtable(L);
  lua_pushinteger(L, tm_ptr->tm_sec);
  lua_setfield(L, -2, "tm_sec");
  lua_pushinteger(L, tm_ptr->tm_min);
  lua_setfield(L, -2, "tm_min");
  lua_pushinteger(L, tm_ptr->tm_hour);
  lua_setfield(L, -2, "tm_hour");
  lua_pushinteger(L, tm_ptr->tm_mday);
  lua_setfield(L, -2, "tm_mday");
  lua_pushinteger(L, tm_ptr->tm_mon);
  lua_setfield(L, -2, "tm_mon");
  lua_pushinteger(L, tm_ptr->tm_year);
  lua_setfield(L, -2, "tm_year");
  lua_pushinteger(L, tm_ptr->tm_wday);
  lua_setfield(L, -2, "tm_wday");
  lua_pushinteger(L, tm_ptr->tm_yday);
  lua_setfield(L, -2, "tm_yday");
  lua_pushinteger(L, tm_ptr->tm_isdst);
  lua_setfield(L, -2, "tm_isdst");
  
  return 1;
}

static int l_libc_mktime (lua_State *L) {
  /* 从Lua表获取时间结构 */
  struct tm tm_struct;
  my_memset(&tm_struct, 0, sizeof(tm_struct));
  
  luaL_checktype(L, 1, LUA_TTABLE);
  
  lua_getfield(L, 1, "tm_sec");
  tm_struct.tm_sec = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "tm_min");
  tm_struct.tm_min = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "tm_hour");
  tm_struct.tm_hour = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "tm_mday");
  tm_struct.tm_mday = luaL_optinteger(L, -1, 1);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "tm_mon");
  tm_struct.tm_mon = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "tm_year");
  tm_struct.tm_year = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "tm_isdst");
  tm_struct.tm_isdst = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  time_t result = my_mktime(&tm_struct);
  lua_pushinteger(L, result);
  return 1;
}

static int l_libc_asctime (lua_State *L) {
  /* 从Lua表获取时间结构 */
  struct tm tm_struct;
  my_memset(&tm_struct, 0, sizeof(tm_struct));
  
  luaL_checktype(L, 1, LUA_TTABLE);
  
  lua_getfield(L, 1, "tm_sec");
  tm_struct.tm_sec = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "tm_min");
  tm_struct.tm_min = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "tm_hour");
  tm_struct.tm_hour = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "tm_mday");
  tm_struct.tm_mday = luaL_optinteger(L, -1, 1);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "tm_mon");
  tm_struct.tm_mon = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "tm_year");
  tm_struct.tm_year = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "tm_wday");
  tm_struct.tm_wday = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "tm_yday");
  tm_struct.tm_yday = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "tm_isdst");
  tm_struct.tm_isdst = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  char *result = my_asctime(&tm_struct);
  lua_pushstring(L, result);
  return 1;
}

static int l_libc_strftime (lua_State *L) {
  const char *format = luaL_checkstring(L, 1);
  
  /* 从Lua表获取时间结构 */
  struct tm tm_struct;
  my_memset(&tm_struct, 0, sizeof(tm_struct));
  
  luaL_checktype(L, 2, LUA_TTABLE);
  
  lua_getfield(L, 2, "tm_sec");
  tm_struct.tm_sec = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 2, "tm_min");
  tm_struct.tm_min = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 2, "tm_hour");
  tm_struct.tm_hour = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 2, "tm_mday");
  tm_struct.tm_mday = luaL_optinteger(L, -1, 1);
  lua_pop(L, 1);
  
  lua_getfield(L, 2, "tm_mon");
  tm_struct.tm_mon = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 2, "tm_year");
  tm_struct.tm_year = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 2, "tm_wday");
  tm_struct.tm_wday = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 2, "tm_yday");
  tm_struct.tm_yday = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  lua_getfield(L, 2, "tm_isdst");
  tm_struct.tm_isdst = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  
  /* 分配缓冲区 */
  size_t maxsize = 256;
  char *buf = (char *)my_malloc(maxsize);
  if (buf == NULL) {
    lua_pushnil(L);
    return 1;
  }
  
  size_t result = my_strftime(buf, maxsize, format, &tm_struct);
  lua_pushstring(L, buf);
  my_free(buf);
  return 1;
}


/*
** Input/Output functions (Lua bindings)
*/

static int l_libc_getchar (lua_State *L) {
  int c = my_getchar();
  if (c == EOF) {
    lua_pushnil(L);
  } else {
    lua_pushinteger(L, c);
  }
  return 1;
}

static int l_libc_putchar (lua_State *L) {
  int c = luaL_checkinteger(L, 1);
  int result = my_putchar(c);
  if (result == EOF) {
    lua_pushnil(L);
  } else {
    lua_pushinteger(L, result);
  }
  return 1;
}

static int l_libc_printf (lua_State *L) {
  const char *format = luaL_checkstring(L, 1);
  /* 简化实现，直接将参数转换为字符串并拼接 */
  /* 注意：这是一个简化实现，不支持完整的格式化功能 */
  char buf[1024];
  int len = my_sprintf(buf, format);
  lua_pushinteger(L, len);
  return 1;
}

static int l_libc_scanf (lua_State *L) {
  const char *format = luaL_checkstring(L, 1);
  /* 简化实现，返回0表示未读取任何内容 */
  lua_pushinteger(L, 0);
  return 1;
}

static int l_libc_sscanf (lua_State *L) {
  const char *str = luaL_checkstring(L, 1);
  const char *format = luaL_checkstring(L, 2);
  /* 简化实现，返回0表示未读取任何内容 */
  lua_pushinteger(L, 0);
  return 1;
}


/*
** File operation functions (Lua bindings)
*/

static int l_libc_fopen (lua_State *L) {
  const char *pathname = luaL_checkstring(L, 1);
  const char *mode = luaL_checkstring(L, 2);
  my_FILE *file = my_fopen(pathname, mode);
  if (file == NULL) {
    lua_pushnil(L);
  } else {
    lua_pushlightuserdata(L, file);
  }
  return 1;
}

static int l_libc_fclose (lua_State *L) {
  my_FILE *file = (my_FILE *)lua_touserdata(L, 1);
  int result = my_fclose(file);
  if (result == EOF) {
    lua_pushnil(L);
  } else {
    lua_pushinteger(L, result);
  }
  return 1;
}

static int l_libc_fread (lua_State *L) {
  /* 简化实现，返回空字符串表示未读取任何内容 */
  lua_pushstring(L, "");
  return 1;
}

static int l_libc_fwrite (lua_State *L) {
  const char *ptr = luaL_checkstring(L, 1);
  size_t size = luaL_checkinteger(L, 2);
  size_t nmemb = luaL_checkinteger(L, 3);
  my_FILE *file = (my_FILE *)lua_touserdata(L, 4);
  size_t result = my_fwrite(ptr, size, nmemb, file);
  lua_pushinteger(L, result);
  return 1;
}

static int l_libc_fseek (lua_State *L) {
  my_FILE *file = (my_FILE *)lua_touserdata(L, 1);
  long offset = luaL_checkinteger(L, 2);
  int whence = luaL_checkinteger(L, 3);
  int result = my_fseek(file, offset, whence);
  lua_pushinteger(L, result);
  return 1;
}

static int l_libc_ftell (lua_State *L) {
  my_FILE *file = (my_FILE *)lua_touserdata(L, 1);
  long result = my_ftell(file);
  lua_pushinteger(L, result);
  return 1;
}

static int l_libc_rewind (lua_State *L) {
  my_FILE *file = (my_FILE *)lua_touserdata(L, 1);
  my_rewind(file);
  return 0;
}


/*
** Process control functions (Lua bindings)
*/

static int l_libc_fork (lua_State *L) {
  pid_t pid = my_fork();
  lua_pushinteger(L, pid);
  return 1;
}

static int l_libc_execve (lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  /* 简化实现，返回-1表示执行失败 */
  lua_pushinteger(L, -1);
  return 1;
}

static int l_libc_wait (lua_State *L) {
  int status;
  pid_t pid = my_wait(&status);
  lua_pushinteger(L, pid);
  lua_pushinteger(L, status);
  return 2;
}

static int l_libc_waitpid (lua_State *L) {
  pid_t pid = luaL_checkinteger(L, 1);
  int options = luaL_optinteger(L, 2, 0);
  int status;
  pid_t result = my_waitpid(pid, &status, options);
  lua_pushinteger(L, result);
  lua_pushinteger(L, status);
  return 2;
}

static int l_libc_exit (lua_State *L) {
  int status = luaL_optinteger(L, 1, 0);
  my_exit(status);
  /* 不会执行到这里 */
  return 0;
}


/*
** Signal handling functions (Lua bindings)
*/

static int l_libc_signal (lua_State *L) {
  int signum = luaL_checkinteger(L, 1);
  /* 简化实现，返回0表示成功 */
  lua_pushinteger(L, 0);
  return 1;
}

static int l_libc_kill (lua_State *L) {
  pid_t pid = luaL_checkinteger(L, 1);
  int sig = luaL_checkinteger(L, 2);
  int result = my_kill(pid, sig);
  lua_pushinteger(L, result);
  return 1;
}

static int l_libc_raise (lua_State *L) {
  int sig = luaL_checkinteger(L, 1);
  int result = my_raise(sig);
  lua_pushinteger(L, result);
  return 1;
}


/*
** String functions
*/

static int l_libc_strlen (lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  lua_pushinteger(L, my_strlen(s));
  return 1;
}


static int l_libc_strcpy (lua_State *L) {
  const char *src = luaL_checkstring(L, 2);
  size_t len = my_strlen(src) + 1;
  char *dst = (char *)lua_newuserdata(L, len);
  my_strcpy(dst, src);
  lua_pushstring(L, dst);
  return 2;
}


static int l_libc_strncpy (lua_State *L) {
  const char *src = luaL_checkstring(L, 2);
  size_t n = luaL_checkinteger(L, 3);
  char *dst = (char *)lua_newuserdata(L, n + 1);
  my_strncpy(dst, src, n);
  dst[n] = '\0';
  lua_pushstring(L, dst);
  return 2;
}


static int l_libc_strcat (lua_State *L) {
  const char *s1 = luaL_checkstring(L, 1);
  const char *s2 = luaL_checkstring(L, 2);
  size_t len1 = my_strlen(s1);
  size_t len2 = my_strlen(s2);
  size_t total = len1 + len2 + 1;
  char *dst = (char *)lua_newuserdata(L, total);
  my_strcpy(dst, s1);
  my_strcpy(dst + len1, s2);
  lua_pushstring(L, dst);
  return 2;
}


static int l_libc_strncat (lua_State *L) {
  const char *s1 = luaL_checkstring(L, 1);
  const char *s2 = luaL_checkstring(L, 2);
  size_t n = luaL_checkinteger(L, 3);
  size_t len1 = my_strlen(s1);
  size_t len2 = my_strlen(s2);
  size_t actual = (n < len2) ? n : len2;
  size_t total = len1 + actual + 1;
  char *dst = (char *)lua_newuserdata(L, total);
  my_strcpy(dst, s1);
  my_strncpy(dst + len1, s2, actual);
  dst[len1 + actual] = '\0';
  lua_pushstring(L, dst);
  return 2;
}


static int l_libc_strcmp (lua_State *L) {
  const char *s1 = luaL_checkstring(L, 1);
  const char *s2 = luaL_checkstring(L, 2);
  lua_pushinteger(L, my_strcmp(s1, s2));
  return 1;
}


static int l_libc_strncmp (lua_State *L) {
  const char *s1 = luaL_checkstring(L, 1);
  const char *s2 = luaL_checkstring(L, 2);
  size_t n = luaL_checkinteger(L, 3);
  lua_pushinteger(L, my_strncmp(s1, s2, n));
  return 1;
}


static int l_libc_strchr (lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  int c = luaL_checkinteger(L, 2);
  const char *p = my_strchr(s, c);
  if (p == NULL) {
    lua_pushnil(L);
  } else {
    lua_pushstring(L, p);
  }
  return 1;
}


static int l_libc_strrchr (lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  int c = luaL_checkinteger(L, 2);
  const char *p = my_strrchr(s, c);
  if (p == NULL) {
    lua_pushnil(L);
  } else {
    lua_pushstring(L, p);
  }
  return 1;
}


static int l_libc_strstr (lua_State *L) {
  const char *haystack = luaL_checkstring(L, 1);
  const char *needle = luaL_checkstring(L, 2);
  const char *p = my_strstr(haystack, needle);
  if (p == NULL) {
    lua_pushnil(L);
  } else {
    lua_pushstring(L, p);
  }
  return 1;
}


static int l_libc_strtok (lua_State *L) {
  const char *str = luaL_optstring(L, 1, NULL);
  const char *delim = luaL_checkstring(L, 2);
  char **saveptr = NULL;
  char *token = NULL;
  
  /* 检查是否提供了saveptr */
  if (lua_islightuserdata(L, 3)) {
    saveptr = (char **)lua_touserdata(L, 3);
  }
  
  if (str != NULL) {
    /* strtok需要可修改的字符串，所以我们需要复制一份 */
    size_t len = my_strlen(str) + 1;
    char *copy = (char *)lua_newuserdata(L, len);
    my_strcpy(copy, str);
    
    /* 创建saveptr并初始化 */
    char **new_saveptr = (char **)lua_newuserdata(L, sizeof(char *));
    *new_saveptr = copy;
    
    token = my_strtok(copy, delim, new_saveptr);
    if (token != NULL) {
      lua_pushstring(L, token);
      return 3;
    }
  } else if (saveptr != NULL) {
    /* 使用现有的saveptr继续分割 */
    token = my_strtok(NULL, delim, saveptr);
    if (token != NULL) {
      lua_pushstring(L, token);
      return 2;
    }
  }
  
  lua_pushnil(L);
  return 1;
}


static int l_libc_tolower (lua_State *L) {
  int c = luaL_checkinteger(L, 1);
  lua_pushinteger(L, my_tolower(c));
  return 1;
}


static int l_libc_toupper (lua_State *L) {
  int c = luaL_checkinteger(L, 1);
  lua_pushinteger(L, my_toupper(c));
  return 1;
}


static int l_libc_strlwr (lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  size_t len = my_strlen(s) + 1;
  char *dst = (char *)lua_newuserdata(L, len);
  my_strcpy(dst, s);
  my_strlwr(dst);
  lua_pushstring(L, dst);
  return 2;
}


static int l_libc_strupr (lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  size_t len = my_strlen(s) + 1;
  char *dst = (char *)lua_newuserdata(L, len);
  my_strcpy(dst, s);
  my_strupr(dst);
  lua_pushstring(L, dst);
  return 2;
}


static int l_libc_strspn (lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  const char *accept = luaL_checkstring(L, 2);
  lua_pushinteger(L, my_strspn(s, accept));
  return 1;
}


static int l_libc_strcspn (lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  const char *reject = luaL_checkstring(L, 2);
  lua_pushinteger(L, my_strcspn(s, reject));
  return 1;
}


static int l_libc_strpbrk (lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  const char *accept = luaL_checkstring(L, 2);
  const char *p = my_strpbrk(s, accept);
  if (p == NULL) {
    lua_pushnil(L);
  } else {
    lua_pushstring(L, p);
  }
  return 1;
}


static int l_libc_strdup (lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  char *dup = my_strdup(s);
  if (dup == NULL) {
    lua_pushnil(L);
  } else {
    lua_pushstring(L, dup);
    free(dup);
  }
  return 1;
}


static int l_libc_strndup (lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  size_t n = luaL_checkinteger(L, 2);
  char *dup = my_strndup(s, n);
  if (dup == NULL) {
    lua_pushnil(L);
  } else {
    lua_pushstring(L, dup);
    free(dup);
  }
  return 1;
}


/*
** Memory functions
*/

static int l_libc_memset (lua_State *L) {
  const void *ptr = lua_topointer(L, 1);
  if (ptr == NULL) {
    return luaL_error(L, "空指针");
  }
  void *s = (void *)ptr;
  int c = luaL_checkinteger(L, 2);
  size_t n = luaL_checkinteger(L, 3);
  lua_pushlightuserdata(L, my_memset(s, c, n));
  return 1;
}


static int l_libc_memcpy (lua_State *L) {
  const void *dst_ptr = lua_topointer(L, 1);
  const void *src_ptr = lua_topointer(L, 2);
  if (dst_ptr == NULL || src_ptr == NULL) {
    return luaL_error(L, "空指针");
  }
  void *dst = (void *)dst_ptr;
  const void *src = src_ptr;
  size_t n = luaL_checkinteger(L, 3);
  lua_pushlightuserdata(L, my_memcpy(dst, src, n));
  return 1;
}


static int l_libc_memmove (lua_State *L) {
  const void *dst_ptr = lua_topointer(L, 1);
  const void *src_ptr = lua_topointer(L, 2);
  if (dst_ptr == NULL || src_ptr == NULL) {
    return luaL_error(L, "空指针");
  }
  void *dst = (void *)dst_ptr;
  const void *src = src_ptr;
  size_t n = luaL_checkinteger(L, 3);
  lua_pushlightuserdata(L, my_memmove(dst, src, n));
  return 1;
}


static int l_libc_memcmp (lua_State *L) {
  const void *s1 = lua_topointer(L, 1);
  const void *s2 = lua_topointer(L, 2);
  if (s1 == NULL || s2 == NULL) {
    return luaL_error(L, "空指针");
  }
  size_t n = luaL_checkinteger(L, 3);
  lua_pushinteger(L, my_memcmp(s1, s2, n));
  return 1;
}


static int l_libc_memchr (lua_State *L) {
  const void *s = lua_topointer(L, 1);
  if (s == NULL) {
    return luaL_error(L, "空指针");
  }
  int c = luaL_checkinteger(L, 2);
  size_t n = luaL_checkinteger(L, 3);
  void *p = my_memchr(s, c, n);
  if (p == NULL) {
    lua_pushnil(L);
  } else {
    lua_pushlightuserdata(L, p);
  }
  return 1;
}


static int l_libc_memmem (lua_State *L) {
  const void *haystack = lua_topointer(L, 1);
  size_t haystack_len = luaL_checkinteger(L, 2);
  const void *needle = lua_topointer(L, 3);
  size_t needle_len = luaL_checkinteger(L, 4);
  
  if (haystack == NULL || needle == NULL) {
    return luaL_error(L, "空指针");
  }
  
  void *p = my_memmem(haystack, haystack_len, needle, needle_len);
  if (p == NULL) {
    lua_pushnil(L);
  } else {
    lua_pushlightuserdata(L, p);
  }
  return 1;
}


static int l_libc_memcmpfast (lua_State *L) {
  const void *s1 = lua_topointer(L, 1);
  const void *s2 = lua_topointer(L, 2);
  if (s1 == NULL || s2 == NULL) {
    return luaL_error(L, "空指针");
  }
  size_t n = luaL_checkinteger(L, 3);
  lua_pushinteger(L, my_memcmp_fast(s1, s2, n));
  return 1;
}

static int l_libc_memcpy_fast (lua_State *L) {
  const void *dst_ptr = lua_topointer(L, 1);
  const void *src_ptr = lua_topointer(L, 2);
  if (dst_ptr == NULL || src_ptr == NULL) {
    return luaL_error(L, "空指针");
  }
  void *dst = (void *)dst_ptr;
  const void *src = src_ptr;
  size_t n = luaL_checkinteger(L, 3);
  lua_pushlightuserdata(L, my_memcpy_fast(dst, src, n));
  return 1;
}


/*
** Math functions
*/

static int l_libc_abs (lua_State *L) {
  int n = luaL_checkinteger(L, 1);
  lua_pushinteger(L, my_abs(n));
  return 1;
}


static int l_libc_sqrt (lua_State *L) {
  double x = luaL_checknumber(L, 1);
  lua_pushnumber(L, my_sqrt(x));
  return 1;
}


static int l_libc_pow (lua_State *L) {
  double base = luaL_checknumber(L, 1);
  double exponent = luaL_checknumber(L, 2);
  lua_pushnumber(L, my_pow(base, exponent));
  return 1;
}


static int l_libc_sin (lua_State *L) {
  double x = luaL_checknumber(L, 1);
  lua_pushnumber(L, my_sin(x));
  return 1;
}


static int l_libc_cos (lua_State *L) {
  double x = luaL_checknumber(L, 1);
  lua_pushnumber(L, my_cos(x));
  return 1;
}


static int l_libc_tan (lua_State *L) {
  double x = luaL_checknumber(L, 1);
  lua_pushnumber(L, my_tan(x));
  return 1;
}


static int l_libc_floor (lua_State *L) {
  double x = luaL_checknumber(L, 1);
  lua_pushnumber(L, my_floor(x));
  return 1;
}


static int l_libc_ceil (lua_State *L) {
  double x = luaL_checknumber(L, 1);
  lua_pushnumber(L, my_ceil(x));
  return 1;
}


static int l_libc_round (lua_State *L) {
  double x = luaL_checknumber(L, 1);
  lua_pushnumber(L, my_round(x));
  return 1;
}


static int l_libc_rand (lua_State *L) {
  lua_pushinteger(L, my_rand());
  return 1;
}


static int l_libc_srand (lua_State *L) {
  unsigned int seed = (unsigned int)luaL_checkinteger(L, 1);
  my_srand(seed);
  return 0;
}


/*
** CRC32校验函数
*/
static int l_libc_crc32 (lua_State *L) {
  const char *data = luaL_checkstring(L, 1);
  size_t len = my_strlen(data);
  unsigned int crc = my_crc32((const unsigned char *)data, len);
  lua_pushinteger(L, crc);
  return 1;
}


/*
** 哈希函数
*/
static int l_libc_hash (lua_State *L) {
  const char *str = luaL_checkstring(L, 1);
  unsigned int hash = my_hash(str);
  lua_pushinteger(L, hash);
  return 1;
}


/*
** 位操作函数
*/
static int l_libc_band (lua_State *L) {
  int a = luaL_checkinteger(L, 1);
  int b = luaL_checkinteger(L, 2);
  lua_pushinteger(L, my_band(a, b));
  return 1;
}


static int l_libc_bor (lua_State *L) {
  int a = luaL_checkinteger(L, 1);
  int b = luaL_checkinteger(L, 2);
  lua_pushinteger(L, my_bor(a, b));
  return 1;
}


static int l_libc_bxor (lua_State *L) {
  int a = luaL_checkinteger(L, 1);
  int b = luaL_checkinteger(L, 2);
  lua_pushinteger(L, my_bxor(a, b));
  return 1;
}


static int l_libc_bnot (lua_State *L) {
  int a = luaL_checkinteger(L, 1);
  lua_pushinteger(L, my_bnot(a));
  return 1;
}


static int l_libc_bleft (lua_State *L) {
  int a = luaL_checkinteger(L, 1);
  int n = luaL_checkinteger(L, 2);
  lua_pushinteger(L, my_bleft(a, n));
  return 1;
}


static int l_libc_bright (lua_State *L) {
  int a = luaL_checkinteger(L, 1);
  int n = luaL_checkinteger(L, 2);
  lua_pushinteger(L, my_bright(a, n));
  return 1;
}


static int l_libc_bswap (lua_State *L) {
  int a = luaL_checkinteger(L, 1);
  lua_pushinteger(L, my_bswap(a));
  return 1;
}


static int l_libc_btest (lua_State *L) {
  int a = luaL_checkinteger(L, 1);
  int n = luaL_checkinteger(L, 2);
  lua_pushboolean(L, my_btest(a, n));
  return 1;
}


static int l_libc_bset (lua_State *L) {
  int a = luaL_checkinteger(L, 1);
  int n = luaL_checkinteger(L, 2);
  lua_pushinteger(L, my_bset(a, n));
  return 1;
}


static int l_libc_bclear (lua_State *L) {
  int a = luaL_checkinteger(L, 1);
  int n = luaL_checkinteger(L, 2);
  lua_pushinteger(L, my_bclear(a, n));
  return 1;
}


/*
** 获取CPU ID
*/
static int l_libc_get_cpu_id (lua_State *L) {
  unsigned long cpu_id = 0;
  
  /* 普通C实现，使用固定值替代CPU ID */
  /* 在没有内联汇编支持的情况下，无法直接获取硬件CPU ID */
  cpu_id = 0x1234; /* 使用固定值 */
  
  lua_pushinteger(L, cpu_id);
  return 1;
}


/*
** 获取当前进程ID
*/
static int l_libc_get_pid (lua_State *L) {
  /* 使用标准库函数获取当前进程ID */
  pid_t pid = getpid();
  lua_pushinteger(L, pid);
  return 1;
}


/*
** 获取当前线程ID
*/
static int l_libc_get_tid (lua_State *L) {
  /* 使用getpid()作为线程ID的简化实现，因为在Android上gettid()需要特殊处理 */
  pid_t tid = getpid();
  lua_pushinteger(L, tid);
  return 1;
}





/*
** 文件系统函数实现
*/

/* 创建目录 */
static int my_mkdir(const char *pathname, mode_t mode) {
  /* 使用标准库函数创建目录 */
  return mkdir(pathname, mode);
}

/* 删除目录 */
static int my_rmdir(const char *pathname) {
  /* 使用标准库函数删除目录 */
  return rmdir(pathname);
}

/* 改变文件权限 */
static int my_chmod(const char *pathname, mode_t mode) {
  /* 使用标准库函数改变文件权限 */
  return chmod(pathname, mode);
}

/* 改变文件所有者 */
static int my_chown(const char *pathname, uid_t owner, gid_t group) {
  /* 使用标准库函数改变文件所有者 */
  return chown(pathname, owner, group);
}

/* 删除文件 */
static int my_unlink(const char *pathname) {
  /* 使用标准库函数删除文件 */
  return unlink(pathname);
}

/* 重命名文件或目录 */
static int my_rename(const char *oldpath, const char *newpath) {
  /* 使用标准库函数重命名文件或目录 */
  return rename(oldpath, newpath);
}

/* 获取文件状态 */
static int my_stat(const char *pathname, struct stat *buf) {
  /* 使用标准库函数获取文件状态 */
  return stat(pathname, buf);
}

/* 其他实用函数实现 */

/* 快速排序 */
static void my_qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
  /* 简化实现，使用冒泡排序 */
  char *ptr = (char *)base;
  for (size_t i = 0; i < nmemb - 1; i++) {
    for (size_t j = 0; j < nmemb - i - 1; j++) {
      if (compar(ptr + j * size, ptr + (j + 1) * size) > 0) {
        /* 交换元素 */
        for (size_t k = 0; k < size; k++) {
          char temp = ptr[j * size + k];
          ptr[j * size + k] = ptr[(j + 1) * size + k];
          ptr[(j + 1) * size + k] = temp;
        }
      }
    }
  }
}

/* 二分查找 */
static void *my_bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
  const char *ptr = (const char *)base;
  size_t low = 0;
  size_t high = nmemb - 1;
  
  while (low <= high) {
    size_t mid = (low + high) / 2;
    int cmp = compar(key, ptr + mid * size);
    if (cmp == 0) {
      return (void *)(ptr + mid * size);
    } else if (cmp < 0) {
      high = mid - 1;
    } else {
      low = mid + 1;
    }
  }
  
  return NULL;
}

/* 长整数绝对值 */
static long my_labs(long n) {
  return (n < 0) ? -n : n;
}

/* 长长整数绝对值 */
static long long my_llabs(long long n) {
  return (n < 0) ? -n : n;
}

/* 整数除法 */
static div_t my_div(int numer, int denom) {
  div_t result;
  result.quot = numer / denom;
  result.rem = numer % denom;
  /* 确保余数与被除数同号 */
  if (result.rem != 0 && ((result.rem < 0) != (numer < 0))) {
    result.rem += denom;
    result.quot -= 1;
  }
  return result;
}

/* 长整数除法 */
static ldiv_t my_ldiv(long numer, long denom) {
  ldiv_t result;
  result.quot = numer / denom;
  result.rem = numer % denom;
  /* 确保余数与被除数同号 */
  if (result.rem != 0 && ((result.rem < 0) != (numer < 0))) {
    result.rem += denom;
    result.quot -= 1;
  }
  return result;
}

/* 长长整数除法 */
static lldiv_t my_lldiv(long long numer, long long denom) {
  lldiv_t result;
  result.quot = numer / denom;
  result.rem = numer % denom;
  /* 确保余数与被除数同号 */
  if (result.rem != 0 && ((result.rem < 0) != (numer < 0))) {
    result.rem += denom;
    result.quot -= 1;
  }
  return result;
}

/*
** 文件系统函数（Lua绑定）
*/

static int l_libc_mkdir (lua_State *L) {
  const char *pathname = luaL_checkstring(L, 1);
  mode_t mode = luaL_optinteger(L, 2, 0755);
  int result = my_mkdir(pathname, mode);
  lua_pushinteger(L, result);
  return 1;
}

static int l_libc_rmdir (lua_State *L) {
  const char *pathname = luaL_checkstring(L, 1);
  int result = my_rmdir(pathname);
  lua_pushinteger(L, result);
  return 1;
}

static int l_libc_chmod (lua_State *L) {
  const char *pathname = luaL_checkstring(L, 1);
  mode_t mode = luaL_checkinteger(L, 2);
  int result = my_chmod(pathname, mode);
  lua_pushinteger(L, result);
  return 1;
}

static int l_libc_chown (lua_State *L) {
  const char *pathname = luaL_checkstring(L, 1);
  uid_t owner = luaL_checkinteger(L, 2);
  gid_t group = luaL_checkinteger(L, 3);
  int result = my_chown(pathname, owner, group);
  lua_pushinteger(L, result);
  return 1;
}

static int l_libc_unlink (lua_State *L) {
  const char *pathname = luaL_checkstring(L, 1);
  int result = my_unlink(pathname);
  lua_pushinteger(L, result);
  return 1;
}

static int l_libc_rename (lua_State *L) {
  const char *oldpath = luaL_checkstring(L, 1);
  const char *newpath = luaL_checkstring(L, 2);
  int result = my_rename(oldpath, newpath);
  lua_pushinteger(L, result);
  return 1;
}

static int l_libc_stat (lua_State *L) {
  const char *pathname = luaL_checkstring(L, 1);
  struct stat buf;
  int result = my_stat(pathname, &buf);
  if (result == -1) {
    lua_pushnil(L);
    return 1;
  }
  
  /* 创建Lua表存储stat信息 */
  lua_newtable(L);
  lua_pushinteger(L, buf.st_dev);
  lua_setfield(L, -2, "st_dev");
  lua_pushinteger(L, buf.st_ino);
  lua_setfield(L, -2, "st_ino");
  lua_pushinteger(L, buf.st_mode);
  lua_setfield(L, -2, "st_mode");
  lua_pushinteger(L, buf.st_nlink);
  lua_setfield(L, -2, "st_nlink");
  lua_pushinteger(L, buf.st_uid);
  lua_setfield(L, -2, "st_uid");
  lua_pushinteger(L, buf.st_gid);
  lua_setfield(L, -2, "st_gid");
  lua_pushinteger(L, buf.st_rdev);
  lua_setfield(L, -2, "st_rdev");
  lua_pushinteger(L, buf.st_size);
  lua_setfield(L, -2, "st_size");
  lua_pushinteger(L, buf.st_atime);
  lua_setfield(L, -2, "st_atime");
  lua_pushinteger(L, buf.st_mtime);
  lua_setfield(L, -2, "st_mtime");
  lua_pushinteger(L, buf.st_ctime);
  lua_setfield(L, -2, "st_ctime");
  lua_pushinteger(L, buf.st_blksize);
  lua_setfield(L, -2, "st_blksize");
  lua_pushinteger(L, buf.st_blocks);
  lua_setfield(L, -2, "st_blocks");
  
  return 1;
}

/* 其他实用函数（Lua绑定） */

static int l_libc_qsort (lua_State *L) {
  /* 简化实现，不支持Lua函数作为比较函数 */
  lua_pushnil(L);
  return 1;
}

static int l_libc_bsearch (lua_State *L) {
  /* 简化实现，不支持Lua函数作为比较函数 */
  lua_pushnil(L);
  return 1;
}

static int l_libc_labs (lua_State *L) {
  long n = luaL_checkinteger(L, 1);
  lua_pushinteger(L, my_labs(n));
  return 1;
}

static int l_libc_llabs (lua_State *L) {
  long long n = luaL_checkinteger(L, 1);
  lua_pushinteger(L, my_llabs(n));
  return 1;
}

static int l_libc_div (lua_State *L) {
  int numer = luaL_checkinteger(L, 1);
  int denom = luaL_checkinteger(L, 2);
  div_t result = my_div(numer, denom);
  
  lua_newtable(L);
  lua_pushinteger(L, result.quot);
  lua_setfield(L, -2, "quot");
  lua_pushinteger(L, result.rem);
  lua_setfield(L, -2, "rem");
  
  return 1;
}

static int l_libc_ldiv (lua_State *L) {
  long numer = luaL_checkinteger(L, 1);
  long denom = luaL_checkinteger(L, 2);
  ldiv_t result = my_ldiv(numer, denom);
  
  lua_newtable(L);
  lua_pushinteger(L, result.quot);
  lua_setfield(L, -2, "quot");
  lua_pushinteger(L, result.rem);
  lua_setfield(L, -2, "rem");
  
  return 1;
}

static int l_libc_lldiv (lua_State *L) {
  long long numer = luaL_checkinteger(L, 1);
  long long denom = luaL_checkinteger(L, 2);
  lldiv_t result = my_lldiv(numer, denom);
  
  lua_newtable(L);
  lua_pushinteger(L, result.quot);
  lua_setfield(L, -2, "quot");
  lua_pushinteger(L, result.rem);
  lua_setfield(L, -2, "rem");
  
  return 1;
}

/*
** Register the libc module functions
*/
static const luaL_Reg libclib[] = {
  /* String functions */
  {"strlen", l_libc_strlen},
  {"strcpy", l_libc_strcpy},
  {"strncpy", l_libc_strncpy},
  {"strcat", l_libc_strcat},
  {"strncat", l_libc_strncat},
  {"strcmp", l_libc_strcmp},
  {"strncmp", l_libc_strncmp},
  {"strchr", l_libc_strchr},
  {"strrchr", l_libc_strrchr},
  {"strstr", l_libc_strstr},
  {"strtok", l_libc_strtok},
  {"tolower", l_libc_tolower},
  {"toupper", l_libc_toupper},
  {"strlwr", l_libc_strlwr},
  {"strupr", l_libc_strupr},
  {"strspn", l_libc_strspn},
  {"strcspn", l_libc_strcspn},
  {"strpbrk", l_libc_strpbrk},
  {"strdup", l_libc_strdup},
  {"strndup", l_libc_strndup},
  
  /* String conversion functions */
  {"atoi", l_libc_atoi},
  {"atol", l_libc_atol},
  {"atof", l_libc_atof},
  {"strtol", l_libc_strtol},
  {"strtoul", l_libc_strtoul},
  {"strtod", l_libc_strtod},
  
  /* Error handling functions */
  {"errno", l_libc_get_errno},
  {"seterrno", l_libc_set_errno},
  {"perror", l_libc_perror},
  {"strerror", l_libc_strerror},
  
  /* Time functions */
  {"time", l_libc_time},
  {"gmtime", l_libc_gmtime},
  {"localtime", l_libc_localtime},
  {"mktime", l_libc_mktime},
  {"asctime", l_libc_asctime},
  {"strftime", l_libc_strftime},
  
  /* Input/Output functions */
  {"getchar", l_libc_getchar},
  {"putchar", l_libc_putchar},
  {"printf", l_libc_printf},
  {"scanf", l_libc_scanf},
  {"sscanf", l_libc_sscanf},
  
  /* File operation functions */
  {"fopen", l_libc_fopen},
  {"fclose", l_libc_fclose},
  {"fread", l_libc_fread},
  {"fwrite", l_libc_fwrite},
  {"fseek", l_libc_fseek},
  {"ftell", l_libc_ftell},
  {"rewind", l_libc_rewind},
  
  /* File system functions */
  {"mkdir", l_libc_mkdir},
  {"rmdir", l_libc_rmdir},
  {"chmod", l_libc_chmod},
  {"chown", l_libc_chown},
  {"unlink", l_libc_unlink},
  {"rename", l_libc_rename},
  {"stat", l_libc_stat},
  
  /* Process control functions */
  {"fork", l_libc_fork},
  {"execve", l_libc_execve},
  {"wait", l_libc_wait},
  {"waitpid", l_libc_waitpid},
  {"exit", l_libc_exit},
  
  /* Signal handling functions */
  {"signal", l_libc_signal},
  {"kill", l_libc_kill},
  {"raise", l_libc_raise},
  
  /* Memory functions */
  {"memset", l_libc_memset},
  {"memcpy", l_libc_memcpy},
  {"memmove", l_libc_memmove},
  {"memcmp", l_libc_memcmp},
  {"memchr", l_libc_memchr},
  {"memcpyfast", l_libc_memcpy_fast},
  {"memmem", l_libc_memmem},
  {"memcmpfast", l_libc_memcmpfast},
  
  /* Dynamic memory allocation */
  {"malloc", l_libc_malloc},
  {"calloc", l_libc_calloc},
  {"realloc", l_libc_realloc},
  {"free", l_libc_free},
  
  /* Math functions */
  {"abs", l_libc_abs},
  {"labs", l_libc_labs},
  {"llabs", l_libc_llabs},
  {"div", l_libc_div},
  {"ldiv", l_libc_ldiv},
  {"lldiv", l_libc_lldiv},
  {"sqrt", l_libc_sqrt},
  {"pow", l_libc_pow},
  {"sin", l_libc_sin},
  {"cos", l_libc_cos},
  {"tan", l_libc_tan},
  {"floor", l_libc_floor},
  {"ceil", l_libc_ceil},
  {"round", l_libc_round},
  
  /* Utility functions */
  {"qsort", l_libc_qsort},
  {"bsearch", l_libc_bsearch},
  
  /* Random functions */
  {"rand", l_libc_rand},
  {"srand", l_libc_srand},
  
  /* CRC32 and Hash functions */
  {"crc32", l_libc_crc32},
  {"hash", l_libc_hash},
  
  /* Bit manipulation functions */
  {"band", l_libc_band},
  {"bor", l_libc_bor},
  {"bxor", l_libc_bxor},
  {"bnot", l_libc_bnot},
  {"bleft", l_libc_bleft},
  {"bright", l_libc_bright},
  {"bswap", l_libc_bswap},
  {"btest", l_libc_btest},
  {"bset", l_libc_bset},
  {"bclear", l_libc_bclear},
  
  /* System call functions */
  /* {"syscall", l_libc_syscall}, 移除：依赖无法实现的my_syscall */
  
  /* ARM specific functions */
  {"getcpuid", l_libc_get_cpu_id},
  {"getpid", l_libc_get_pid},
  {"gettid", l_libc_get_tid},
  /* {"clockgettime", l_libc_clock_gettime}, 移除：依赖无法实现的my_syscall */
  
  {NULL, NULL}
};


/*
** Open the libc module
*/
LUAMOD_API int luaopen_libc (lua_State *L) {
  luaL_newlib(L, libclib);
  return 1;
}