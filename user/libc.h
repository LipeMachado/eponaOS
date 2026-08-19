#ifndef USER_LIBC_H
#define USER_LIBC_H

#include <stdint.h>

#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_OPEN    2
#define SYS_READ    3
#define SYS_CLOSE   4
#define SYS_READDIR 5
#define SYS_EXEC    6
#define SYS_CREATE  7
#define SYS_UNLINK  8
#define SYS_STAT    9
#define SYS_PIPE    10
#define SYS_FORK    11
#define SYS_WAIT    12
#define SYS_GETPID  13
#define SYS_GETPPID 14
#define SYS_KILL    15
#define SYS_SIGNAL  16
#define SYS_BRK     17
#define SYS_DUP2    18
#define SYS_CHDIR   19
#define SYS_GETCWD  20
#define SYS_REBOOT  21

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SIGINT  2
#define SIGTERM 15
#define SIGKILL 9

#define REBOOT_CMD_SHUTDOWN 0x08
#define REBOOT_CMD_REBOOT   0x10

static inline void sys_exit(int code) {
    register uint64_t rax __asm__("rax") = SYS_EXIT;
    register uint64_t rdi __asm__("rdi") = code;
    __asm__ volatile("int $0x80" : : "r"(rax), "r"(rdi) : "memory");
    __builtin_unreachable();
}

static inline int sys_write(int fd, const void *buf, uint64_t count) {
    register uint64_t rax __asm__("rax") = SYS_WRITE;
    register uint64_t rdi __asm__("rdi") = fd;
    register uint64_t rsi __asm__("rsi") = (uint64_t)buf;
    register uint64_t rdx __asm__("rdx") = count;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi), "r"(rsi), "r"(rdx) : "memory");
    return (int)rax;
}

static inline int sys_open(const char *path) {
    register uint64_t rax __asm__("rax") = SYS_OPEN;
    register uint64_t rdi __asm__("rdi") = (uint64_t)path;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi) : "memory");
    return (int)rax;
}

static inline int sys_read(int fd, void *buf, uint64_t count) {
    register uint64_t rax __asm__("rax") = SYS_READ;
    register uint64_t rdi __asm__("rdi") = fd;
    register uint64_t rsi __asm__("rsi") = (uint64_t)buf;
    register uint64_t rdx __asm__("rdx") = count;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi), "r"(rsi), "r"(rdx) : "memory");
    return (int)rax;
}

static inline int sys_close(int fd) {
    register uint64_t rax __asm__("rax") = SYS_CLOSE;
    register uint64_t rdi __asm__("rdi") = fd;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi) : "memory");
    return (int)rax;
}

static inline int sys_readdir(const char *path, char *buf, uint64_t count) {
    register uint64_t rax __asm__("rax") = SYS_READDIR;
    register uint64_t rdi __asm__("rdi") = (uint64_t)path;
    register uint64_t rsi __asm__("rsi") = (uint64_t)buf;
    register uint64_t rdx __asm__("rdx") = count;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi), "r"(rsi), "r"(rdx) : "memory");
    return (int)rax;
}

static inline int sys_exec(const char *path) {
    register uint64_t rax __asm__("rax") = SYS_EXEC;
    register uint64_t rdi __asm__("rdi") = (uint64_t)path;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi) : "memory");
    return (int)rax;
}

static inline int sys_create(const char *path) {
    register uint64_t rax __asm__("rax") = SYS_CREATE;
    register uint64_t rdi __asm__("rdi") = (uint64_t)path;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi) : "memory");
    return (int)rax;
}

static inline int sys_unlink(const char *path) {
    register uint64_t rax __asm__("rax") = SYS_UNLINK;
    register uint64_t rdi __asm__("rdi") = (uint64_t)path;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi) : "memory");
    return (int)rax;
}

static inline int sys_stat(const char *path, uint32_t *size, uint8_t *flags) {
    register uint64_t rax __asm__("rax") = SYS_STAT;
    register uint64_t rdi __asm__("rdi") = (uint64_t)path;
    register uint64_t rsi __asm__("rsi") = (uint64_t)size;
    register uint64_t rdx __asm__("rdx") = (uint64_t)flags;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi), "r"(rsi), "r"(rdx) : "memory");
    return (int)rax;
}

static inline int sys_pipe(int fds[2]) {
    register uint64_t rax __asm__("rax") = SYS_PIPE;
    register uint64_t rdi __asm__("rdi") = (uint64_t)fds;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi) : "memory");
    return (int)rax;
}

static inline int sys_fork(void) {
    register uint64_t rax __asm__("rax") = SYS_FORK;
    __asm__ volatile("int $0x80" : "+r"(rax) : : "memory");
    return (int)rax;
}

static inline int sys_wait(int *status) {
    register uint64_t rax __asm__("rax") = SYS_WAIT;
    register uint64_t rdi __asm__("rdi") = (uint64_t)status;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi) : "memory");
    return (int)rax;
}

static inline int sys_getpid(void) {
    register uint64_t rax __asm__("rax") = SYS_GETPID;
    __asm__ volatile("int $0x80" : "+r"(rax) : : "memory");
    return (int)rax;
}

static inline int sys_getppid(void) {
    register uint64_t rax __asm__("rax") = SYS_GETPPID;
    __asm__ volatile("int $0x80" : "+r"(rax) : : "memory");
    return (int)rax;
}

static inline int sys_kill(int pid, int sig) {
    register uint64_t rax __asm__("rax") = SYS_KILL;
    register uint64_t rdi __asm__("rdi") = pid;
    register uint64_t rsi __asm__("rsi") = sig;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi), "r"(rsi) : "memory");
    return (int)rax;
}

static inline void (*sys_signal(int sig, void (*handler)(int)))(int) {
    register uint64_t rax __asm__("rax") = SYS_SIGNAL;
    register uint64_t rdi __asm__("rdi") = sig;
    register uint64_t rsi __asm__("rsi") = (uint64_t)handler;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi), "r"(rsi) : "memory");
    (void)rax;
    return handler;
}

static inline int sys_dup2(int oldfd, int newfd) {
    register uint64_t rax __asm__("rax") = SYS_DUP2;
    register uint64_t rdi __asm__("rdi") = oldfd;
    register uint64_t rsi __asm__("rsi") = newfd;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi), "r"(rsi) : "memory");
    return (int)rax;
}

static inline int sys_chdir(const char *path) {
    register uint64_t rax __asm__("rax") = SYS_CHDIR;
    register uint64_t rdi __asm__("rdi") = (uint64_t)path;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi) : "memory");
    return (int)rax;
}

static inline int sys_getcwd(char *buf, uint64_t size) {
    register uint64_t rax __asm__("rax") = SYS_GETCWD;
    register uint64_t rdi __asm__("rdi") = (uint64_t)buf;
    register uint64_t rsi __asm__("rsi") = size;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi), "r"(rsi) : "memory");
    return (int)rax;
}

static inline int sys_reboot(uint64_t cmd) {
    register uint64_t rax __asm__("rax") = SYS_REBOOT;
    register uint64_t rdi __asm__("rdi") = cmd;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi) : "memory");
    return (int)rax;
}

static inline uint64_t sys_brk(uint64_t addr) {
    register uint64_t rax __asm__("rax") = SYS_BRK;
    register uint64_t rdi __asm__("rdi") = addr;
    __asm__ volatile("int $0x80" : "+r"(rax) : "r"(rdi) : "memory");
    return rax;
}

#endif
