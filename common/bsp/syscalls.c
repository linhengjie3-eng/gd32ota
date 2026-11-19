#include <sys/stat.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include "bsp_uart.h"

/*
 * newlib retarget。工程同时链接 -lnosys (与 GD32 官方 GCC Makefile 一致),
 * 这里用 weak 定义, 避免与 libnosys.a 里同名的强符号重复定义;
 * 因为我们目标文件里的符号先被链接器看到, 实际生效的是这里的调试串口实现。
 */
#if defined(__GNUC__)
#define SYS_WEAK __attribute__((weak))
#else
#define SYS_WEAK
#endif

extern uint32_t _end;
extern uint32_t _estack;
static uint8_t *heap_ptr;

SYS_WEAK int _write(int file, char *data, int length)
{
    (void)file;
    bsp_debug_uart_write((const uint8_t *)data, (size_t)length);
    return length;
}

/* Keil/MDK + MicroLIB 用 fputc 重定向; GCC/newlib 用 _write, 不要重复定义 */
#if !defined(__GNUC__)
int fputc(int ch, void *stream)
{
    (void)stream;
    return bsp_debug_putchar(ch);
}
#endif

SYS_WEAK void *_sbrk(ptrdiff_t increment)
{
    uint8_t *previous;
    if (heap_ptr == 0) heap_ptr = (uint8_t *)&_end;
    previous = heap_ptr;
    if ((heap_ptr + increment) > ((uint8_t *)&_estack - 4096)) {
        errno = ENOMEM;
        return (void *)-1;
    }
    heap_ptr += increment;
    return previous;
}

SYS_WEAK int _close(int file) { (void)file; return -1; }
SYS_WEAK int _fstat(int file, struct stat *st) { (void)file; st->st_mode = S_IFCHR; return 0; }
SYS_WEAK int _isatty(int file) { (void)file; return 1; }
SYS_WEAK int _lseek(int file, int ptr, int dir) { (void)file; (void)ptr; (void)dir; return 0; }
SYS_WEAK int _read(int file, char *ptr, int len) { (void)file; (void)ptr; (void)len; return 0; }
SYS_WEAK int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
SYS_WEAK int _getpid(void) { return 1; }
SYS_WEAK void _exit(int status) { (void)status; for (;;) {} }
