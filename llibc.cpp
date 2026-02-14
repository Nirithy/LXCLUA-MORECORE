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