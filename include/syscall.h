#ifndef EPONA_SYSCALL_H
#define EPONA_SYSCALL_H

#include <stdint.h>
#include <stddef.h>

/* Syscall numbers */
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

/* FD types */
#define FD_STDIN     0
#define FD_STDOUT    1
#define FD_STDERR    2
#define FD_TYPE_FILE  1
#define FD_TYPE_PIPE_R 2
#define FD_TYPE_PIPE_W 3

/* Signal numbers */
#define SIGINT  2
#define SIGTERM 15
#define SIGKILL 9

/* Reboot commands */
#define REBOOT_CMD_SHUTDOWN 0x08
#define REBOOT_CMD_REBOOT   0x10

void enter_usermode(void *func, void *stack);
void enter_usermode_save_ret(void *func, void *stack, void *pml4);
void syscall_handler_c(void *regs);

extern uint64_t g_elf_ret_rip;
extern uint64_t g_elf_ret_rsp;
extern uint64_t g_kernel_cr3;

#endif
