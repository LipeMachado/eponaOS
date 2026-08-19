#include "scheduler.h"
#include "pmm.h"
#include "gpu.h"
#include "serial.h"
#include "pit.h"
#include "vfs.h"
#include "heap.h"
#include <stddef.h>
#include <stdint.h>

extern uint64_t g_kernel_cr3;

static task_struct_t *g_current = NULL;
static uint32_t g_next_pid = 1;

extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);

/* ---- Per-process FD helpers ---- */

int task_fd_alloc(task_struct_t *task, void *ptr, int type) {
    if (!task) return -1;
    for (int i = 3; i < TASK_MAX_FD; i++) {
        if (!task->fd[i].used) {
            task->fd[i].used = 1;
            task->fd[i].ptr = ptr;
            task->fd[i].type = type;
            return i;
        }
    }
    return -1;
}

void task_fd_free(task_struct_t *task, int fd) {
    if (!task || fd < 0 || fd >= TASK_MAX_FD) return;
    task->fd[fd].used = 0;
    task->fd[fd].ptr = NULL;
    task->fd[fd].type = 0;
}

void *task_fd_get(task_struct_t *task, int fd) {
    if (!task || fd < 0 || fd >= TASK_MAX_FD || !task->fd[fd].used)
        return NULL;
    return task->fd[fd].ptr;
}

void task_fd_close_all(task_struct_t *task) {
    if (!task) return;
    for (int i = 3; i < TASK_MAX_FD; i++) {
        if (!task->fd[i].used) continue;
        if (task->fd[i].type == 1 /* FD_TYPE_FILE */) {
            file_t *f = (file_t *)task->fd[i].ptr;
            if (f) vfs_close(f);
        }
        /* pipe refcounting handled by caller */
        task->fd[i].used = 0;
        task->fd[i].ptr = NULL;
        task->fd[i].type = 0;
    }
}

/* ---- Task lifecycle ---- */

uint32_t task_create(void (*entry)(void)) {
    /* Allocate task struct + stack in kernel pages */
    task_struct_t *task = (task_struct_t *) pmm_alloc();
    uint64_t *stack = (uint64_t *) pmm_alloc();

    if (!task || !stack)
        return 0;

    /* Zero out the task struct */
    for (size_t i = 0; i < sizeof(task_struct_t); i++)
        ((uint8_t *)task)[i] = 0;

    uint64_t stack_top = (uint64_t) stack + TASK_STACK_SIZE;

    /* Initial stack frame: entry point + 6 saved registers (all zero) */
    stack_top -= 8;
    *(uint64_t *) stack_top = (uint64_t) entry;

    for (int i = 0; i < 6; i++) {
        stack_top -= 8;
        *(uint64_t *) stack_top = 0;
    }

    task->pid   = g_next_pid++;
    task->ppid  = g_current ? g_current->pid : 0;
    task->state = TASK_READY;
    task->rsp   = stack_top;
    task->cr3   = 0;
    task->entry = entry;
    task->exit_code = 0;
    task->kernel_stack_base = (uint64_t)stack;

    /* Initialize FD table */
    for (int i = 0; i < TASK_MAX_FD; i++)
        task->fd[i].used = 0;

    /* Initialize signal table */
    for (int i = 0; i < TASK_MAX_SIGNALS; i++) {
        task->signals[i].handler = NULL;
        task->signals[i].pending = 0;
        task->signals[i].mask = 0;
    }

    /* Insert into circular linked list */
    if (g_current == NULL) {
        task->next = task;
        g_current = task;
    } else {
        task->next = g_current->next;
        g_current->next = task;
    }

    return task->pid;
}

void task_destroy(task_struct_t *task) {
    if (!task) return;

    /* Unlink from circular list */
    task_struct_t *prev = g_current;
    while (prev && prev->next != task)
        prev = prev->next;

    if (prev && prev->next == task) {
        prev->next = task->next;
        if (g_current == task)
            g_current = task->next;
    }

    /* Free kernel stack */
    if (task->kernel_stack_base)
        pmm_free((void *)task->kernel_stack_base);

    /* Free task struct */
    pmm_free(task);
}

task_struct_t *task_current(void) {
    return g_current;
}

task_struct_t *task_find(uint32_t pid) {
    if (!g_current) return NULL;
    task_struct_t *t = g_current;
    do {
        if (t->pid == pid) return t;
        t = t->next;
    } while (t != g_current);
    return NULL;
}

void schedule(void) {
    if (g_current == NULL)
        return;

    task_struct_t *start = g_current;
    task_struct_t *next  = g_current->next;

    /* Skip terminated and sleeping tasks */
    while (next != start && (next->state == TASK_TERMINATED ||
           next->state == TASK_SLEEPING)) {
        next = next->next;
    }

    /* Clean up terminated tasks */
    task_struct_t *scan = next;
    while (scan != start) {
        task_struct_t *n = scan->next;
        if (scan->state == TASK_TERMINATED) {
            task_fd_close_all(scan);
            /* Unlink */
            task_struct_t *prev = g_current;
            while (prev && prev->next != scan)
                prev = prev->next;
            if (prev && prev->next == scan)
                prev->next = scan->next;
            pmm_free((void *)scan->kernel_stack_base);
            pmm_free(scan);
        }
        scan = n;
        if (scan == start) break;
    }

    if (next == start) {
        if (start->state == TASK_TERMINATED) {
            /* last task terminated — just halt */
            __asm__ volatile("cli; hlt");
        }
        return;
    }

    task_struct_t *prev = g_current;
    prev->state = TASK_READY;
    next->state = TASK_RUNNING;
    g_current   = next;

    uint64_t next_rsp = next->rsp;
    uint64_t next_cr3 = next->cr3 ? next->cr3 : g_kernel_cr3;
    uint64_t cur_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cur_cr3));
    prev->cr3 = cur_cr3;
    if (next_cr3 != cur_cr3)
        __asm__ volatile("mov %0, %%cr3" :: "r"(next_cr3) : "memory");

    context_switch(&prev->rsp, next_rsp);
}

void task_yield(void) {
    schedule();
}

void task_sleep(uint64_t ms) {
    if (!g_current) return;
    uint64_t ticks_to_sleep = ms / 10;
    if (ticks_to_sleep == 0) ticks_to_sleep = 1;
    g_current->wake_tick = pit_ticks() + ticks_to_sleep;
    g_current->state = TASK_SLEEPING;
    task_yield();
}

void scheduler_tick(void) {
    uint64_t now = pit_ticks();
    if (!g_current) return;
    task_struct_t *t = g_current;
    do {
        if (t->state == TASK_SLEEPING && now >= t->wake_tick)
            t->state = TASK_READY;

        /* Signal delivery: check pending signals */
        if (t->state != TASK_TERMINATED) {
            for (int s = 0; s < TASK_MAX_SIGNALS; s++) {
                if (t->signals[s].pending && !t->signals[s].mask) {
                    t->signals[s].pending = 0;
                    if (s == 9 /* SIGKILL */) {
                        t->state = TASK_TERMINATED;
                        t->exit_code = -9;
                        break;
                    }
                    if (t->signals[s].handler) {
                        /* For now, just kill on SIGINT/SIGTERM with default behavior */
                        if (s == 2 /* SIGINT */ || s == 15 /* SIGTERM */) {
                            t->state = TASK_TERMINATED;
                            t->exit_code = -s;
                            break;
                        }
                    }
                }
            }
        }

        t = t->next;
    } while (t != g_current);
}

static void idle_task(void) {
    while (1) {
        __asm__ volatile("hlt");
    }
}

void scheduler_init(void) {
    serial_print("[scheduler] init\n");
    __asm__ volatile("mov %%cr3, %0" : "=r"(g_kernel_cr3));
    task_create(idle_task);
    g_current->state = TASK_RUNNING;
    serial_print("[scheduler] pronto (per-process FD, 16KiB stacks)\n");
}
