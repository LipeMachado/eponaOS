#ifndef EPONA_SCHEDULER_H
#define EPONA_SCHEDULER_H

#include <stdint.h>
#include <stddef.h>

#define TASK_STACK_SIZE (16 * 1024)  /* 16 KiB — seguro para callbacks pesados */
#define TASK_MAX_FD     32
#define TASK_MAX_SIGNALS 32

typedef enum {
    TASK_READY      = 0,
    TASK_RUNNING    = 1,
    TASK_BLOCKED    = 2,
    TASK_TERMINATED = 3,
    TASK_SLEEPING   = 4
} task_state_t;

/* File descriptor entry — per-process */
typedef struct {
    void *ptr;
    int   type;     /* FD_TYPE_* from syscall.h, 0 = unused */
    int   used;
} task_fd_entry_t;

/* Signal handler entry */
typedef struct {
    void (*handler)(int);
    uint32_t pending;
    uint32_t mask;
} task_signal_t;

typedef struct task_struct {
    uint32_t       pid;
    uint32_t       ppid;        /* parent PID */
    task_state_t   state;
    uint64_t       rsp;         /* stack pointer (RSP) quando nao executando */
    uint64_t       cr3;         /* CR3 (PML4 phys) for this task, 0 = kernel */
    void           (*entry)(void);
    uint64_t       wake_tick;
    int            exit_code;
    task_fd_entry_t fd[TASK_MAX_FD];
    task_signal_t  signals[TASK_MAX_SIGNALS];
    uint64_t       kernel_stack_base;  /* base of allocated kernel stack */
    struct task_struct *next;
    struct task_struct *children;
    struct task_struct *next_child;
} task_struct_t;

void scheduler_init(void);
uint32_t task_create(void (*entry)(void));
void task_destroy(task_struct_t *task);
void schedule(void);
void task_yield(void);
void task_sleep(uint64_t ms);
void scheduler_tick(void);
task_struct_t *task_current(void);
task_struct_t *task_find(uint32_t pid);

int  task_fd_alloc(task_struct_t *task, void *ptr, int type);
void task_fd_free(task_struct_t *task, int fd);
void *task_fd_get(task_struct_t *task, int fd);
void task_fd_close_all(task_struct_t *task);

#endif
