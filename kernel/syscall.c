#include "syscall.h"
#include "elf.h"
#include "gpu.h"
#include "term.h"
#include "serial.h"
#include "vfs.h"
#include "heap.h"
#include "keyboard.h"
#include "scheduler.h"
#include "pmm.h"
#include "paging.h"
#include "string.h"
#include <stdint.h>
#include <stddef.h>

#define MAX_PIPES 16
#define PIPE_BUF_SIZE 4096

struct regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

uint64_t g_elf_ret_rip = 0;
uint64_t g_elf_ret_rsp = 0;
uint64_t g_kernel_cr3 = 0;

uint64_t g_elf_saved_rbx = 0;
uint64_t g_elf_saved_rbp = 0;
uint64_t g_elf_saved_r12 = 0;
uint64_t g_elf_saved_r13 = 0;
uint64_t g_elf_saved_r14 = 0;
uint64_t g_elf_saved_r15 = 0;

typedef struct {
    uint8_t buf[PIPE_BUF_SIZE];
    uint32_t head;
    uint32_t count;
    int readers;
    int writers;
    int used;
} pipe_t;

static pipe_t g_pipes[MAX_PIPES];

static pipe_t *pipe_alloc(void) {
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!g_pipes[i].used) {
            g_pipes[i].used = 1;
            g_pipes[i].head = 0;
            g_pipes[i].count = 0;
            g_pipes[i].readers = 1;
            g_pipes[i].writers = 1;
            return &g_pipes[i];
        }
    }
    return NULL;
}

static int pipe_read(pipe_t *p, uint64_t count, void *buf) {
    uint8_t *dst = (uint8_t *)buf;
    uint64_t n = 0;
    while (n < count && p->count > 0) {
        uint32_t tail = (p->head + PIPE_BUF_SIZE - p->count) % PIPE_BUF_SIZE;
        dst[n++] = p->buf[tail];
        p->count--;
    }
    return (int)n;
}

static int pipe_write(pipe_t *p, uint64_t count, const void *buf) {
    const uint8_t *src = (const uint8_t *)buf;
    uint64_t n = 0;
    while (n < count && p->count < PIPE_BUF_SIZE) {
        p->buf[p->head] = src[n++];
        p->head = (p->head + 1) % PIPE_BUF_SIZE;
        p->count++;
    }
    return (int)n;
}

/* Validate user pointer: must be in user address range (below 0x7FFFFFFFFF) */
static int validate_user_ptr(const void *ptr, uint64_t size) {
    uint64_t addr = (uint64_t)ptr;
    if (addr < 0x1000) return 0; /* null page */
    if (addr + size < addr) return 0; /* overflow */
    if (addr + size > 0x7FFFFFFFFFULL) return 0; /* above user range */
    /* Check that every page in range is mapped in current address space */
    for (uint64_t page = addr & ~0xFFFULL; page < ((addr + size + 0xFFF) & ~0xFFFULL); page += 0x1000) {
        if (!paging_get_phys(page)) return 0;
    }
    return 1;
}

/* Copy string from user, byte by byte, with bounds check */
static int copy_from_user(char *kbuf, const char *user_ptr, uint64_t max) {
    if (!user_ptr || max == 0) return -1;
    if (!validate_user_ptr(user_ptr, 1)) return -1;
    uint64_t i = 0;
    while (i < max - 1) {
        char c = user_ptr[i];
        kbuf[i] = c;
        if (c == 0) return 0;
        i++;
    }
    kbuf[i] = 0;
    return 0;
}

/* Copy data from user to kernel */
static int __attribute__((unused)) copy_data_from_user(void *kdst, const void *usrc, uint64_t size) {
    if (!validate_user_ptr(usrc, size)) return -1;
    for (uint64_t i = 0; i < size; i++)
        ((uint8_t *)kdst)[i] = ((const uint8_t *)usrc)[i];
    return 0;
}

/* Copy data from kernel to user */
static int __attribute__((unused)) copy_data_to_user(void *udst, const void *ksrc, uint64_t size) {
    if (!validate_user_ptr(udst, size)) return -1;
    for (uint64_t i = 0; i < size; i++)
        ((uint8_t *)udst)[i] = ((const uint8_t *)ksrc)[i];
    return 0;
}

static int resolve_user_path(const char *user_path, char *out, int max) {
    if (!user_path || !validate_user_ptr(user_path, 1)) return -1;
    char path[256];
    if (copy_from_user(path, user_path, 256) < 0) return -1;
    if (path[0] == '/') {
        int i;
        for (i = 0; i < max - 1 && path[i]; i++)
            out[i] = path[i];
        out[i] = 0;
    } else {
        out[0] = '/';
        int i;
        for (i = 0; i < max - 2 && path[i]; i++)
            out[1 + i] = path[i];
        out[1 + i] = 0;
    }
    return 0;
}

struct readdir_ctx {
    char *buf;
    uint64_t cap;
    uint64_t used;
};

static void readdir_append(struct readdir_ctx *ctx, const char *s) {
    while (*s && ctx->used + 1 < ctx->cap)
        ctx->buf[ctx->used++] = *s++;
}

static int readdir_cb(const char *name, uint32_t size, uint8_t flags, void *arg) {
    struct readdir_ctx *ctx = (struct readdir_ctx *)arg;
    char num[11];
    int i = 10;

    readdir_append(ctx, name);
    if (flags & VFS_DIR) {
        readdir_append(ctx, "/");
    } else {
        num[i] = 0;
        if (size == 0) num[--i] = '0';
        while (size && i > 0) {
            num[--i] = (char)('0' + size % 10);
            size /= 10;
        }
        readdir_append(ctx, "  ");
        readdir_append(ctx, &num[i]);
        readdir_append(ctx, " bytes");
    }
    readdir_append(ctx, "\n");
    return 0;
}

void syscall_handler_c(void *vregs) {
    struct regs *r = (struct regs *) vregs;
    task_struct_t *cur = task_current();

    switch (r->rax) {

    /* ---- EXIT ---- */
    case SYS_EXIT:
        gpu_print("[syscall] exit\n");
        serial_print("[syscall] exit\n");
        if (cur) {
            task_fd_close_all(cur);
            cur->exit_code = (int)r->rdi;
            cur->state = TASK_TERMINATED;
        }
        if (g_elf_ret_rip) {
            r->cs = 0x08;
        } else {
            __asm__ volatile("cli; hlt");
        }
        break;

    /* ---- WRITE ---- */
    case SYS_WRITE: {
        int fd = (int)r->rdi;
        if (fd == FD_STDOUT || fd == FD_STDERR) {
            if (!validate_user_ptr((void *)r->rsi, r->rdx)) { r->rax = -1; break; }
            for (uint64_t i = 0; i < r->rdx; i++) {
                char c = ((char *)r->rsi)[i];
                term_putc(c);
                serial_putc(c);
            }
            r->rax = r->rdx;
        } else {
            if (!cur) { r->rax = -1; break; }
            void *fobj = task_fd_get(cur, fd);
            if (!fobj) { r->rax = -1; break; }
            int ftype = cur->fd[fd].type;
            if (ftype == FD_TYPE_PIPE_W) {
                pipe_t *p = (pipe_t *)fobj;
                if (!validate_user_ptr((void *)r->rsi, r->rdx)) { r->rax = -1; break; }
                while (p->count == PIPE_BUF_SIZE && p->readers > 0)
                    task_yield();
                if (p->readers == 0) { r->rax = -1; break; }
                r->rax = (uint64_t)pipe_write(p, r->rdx, (void *)r->rsi);
            } else if (ftype == FD_TYPE_FILE) {
                file_t *f = (file_t *)fobj;
                if (!validate_user_ptr((void *)r->rsi, r->rdx)) { r->rax = -1; break; }
                int n = vfs_write(f, r->rdx, (void *)r->rsi);
                r->rax = (uint64_t)(n < 0 ? -1 : n);
            } else {
                r->rax = -1;
            }
        }
        break;
    }

    /* ---- OPEN ---- */
    case SYS_OPEN: {
        char path[256];
        if (resolve_user_path((const char *)r->rdi, path, 256) < 0) { r->rax = -1; break; }
        file_t *f = vfs_open(path);
        if (f) {
            if (!cur) { vfs_close(f); r->rax = -1; break; }
            r->rax = (uint64_t)task_fd_alloc(cur, f, FD_TYPE_FILE);
            if ((int)r->rax < 0) vfs_close(f);
        } else {
            r->rax = -1;
        }
        break;
    }

    /* ---- CREATE ---- */
    case SYS_CREATE: {
        char path[256];
        if (resolve_user_path((const char *)r->rdi, path, 256) < 0) { r->rax = -1; break; }
        file_t *f = vfs_create(path);
        if (f) {
            if (!cur) { vfs_close(f); r->rax = -1; break; }
            r->rax = (uint64_t)task_fd_alloc(cur, f, FD_TYPE_FILE);
            if ((int)r->rax < 0) vfs_close(f);
        } else {
            r->rax = -1;
        }
        break;
    }

    /* ---- READ ---- */
    case SYS_READ: {
        int fd = (int)r->rdi;
        if (fd == FD_STDIN) {
            if (!validate_user_ptr((void *)r->rsi, r->rdx)) { r->rax = -1; break; }
            char *buf = (char *)r->rsi;
            uint64_t read = 0;
            while (read < r->rdx) {
                int c = keyboard_getc();
                if (!c) continue;
                if (c > 255) continue;
                buf[read++] = (char)c;
                if (c == '\n') break;
            }
            r->rax = read;
        } else {
            if (!cur) { r->rax = -1; break; }
            void *fobj = task_fd_get(cur, fd);
            if (!fobj) { r->rax = -1; break; }
            int ftype = cur->fd[fd].type;
            if (ftype == FD_TYPE_PIPE_R) {
                pipe_t *p = (pipe_t *)fobj;
                if (!validate_user_ptr((void *)r->rsi, r->rdx)) { r->rax = -1; break; }
                while (p->count == 0 && p->writers > 0)
                    task_yield();
                if (p->count == 0) { r->rax = 0; break; }
                r->rax = (uint64_t)pipe_read(p, r->rdx, (void *)r->rsi);
            } else if (ftype == FD_TYPE_FILE) {
                file_t *f = (file_t *)fobj;
                if (!validate_user_ptr((void *)r->rsi, r->rdx)) { r->rax = -1; break; }
                int n = vfs_read(f, r->rdx, (void *)r->rsi);
                r->rax = (uint64_t)(n < 0 ? -1 : n);
            } else {
                r->rax = -1;
            }
        }
        break;
    }

    /* ---- CLOSE ---- */
    case SYS_CLOSE: {
        int fd = (int)r->rdi;
        if (!cur) { r->rax = -1; break; }
        if (fd < 0 || fd >= TASK_MAX_FD || !cur->fd[fd].used) { r->rax = -1; break; }
        int ftype = cur->fd[fd].type;
        if (ftype == FD_TYPE_PIPE_R || ftype == FD_TYPE_PIPE_W) {
            pipe_t *p = (pipe_t *)cur->fd[fd].ptr;
            if (ftype == FD_TYPE_PIPE_R) p->readers--;
            else p->writers--;
            if (p->readers == 0 && p->writers == 0) p->used = 0;
        } else if (ftype == FD_TYPE_FILE) {
            vfs_close((file_t *)cur->fd[fd].ptr);
        }
        task_fd_free(cur, fd);
        r->rax = 0;
        break;
    }

    /* ---- READDIR ---- */
    case SYS_READDIR: {
        char path[256];
        if (resolve_user_path((const char *)r->rdi, path, 256) < 0) { r->rax = -1; break; }
        if (!validate_user_ptr((void *)r->rsi, r->rdx)) { r->rax = -1; break; }
        char *out = (char *)r->rsi;
        uint64_t cap = r->rdx;
        struct readdir_ctx ctx = {.buf = out, .cap = cap, .used = 0};
        if (vfs_readdir(path, readdir_cb, &ctx) < 0) {
            out[0] = 0;
            r->rax = -1;
            break;
        }
        out[ctx.used < cap ? ctx.used : cap - 1] = 0;
        r->rax = ctx.used;
        break;
    }

    /* ---- EXEC ---- */
    case SYS_EXEC: {
        char path[256];
        if (resolve_user_path((const char *)r->rdi, path, 256) < 0) { r->rax = -1; break; }
        uint64_t entry, stack_top, pml4;
        if (elf_load(path, &entry, &stack_top, &pml4) < 0) {
            r->rax = -1;
            break;
        }
        if (cur) task_fd_close_all(cur);
        r->rip = entry;
        r->rsp = stack_top;
        r->rax = 0;
        __asm__ volatile("mov %0, %%cr3" :: "r"(pml4) : "memory");
        break;
    }

    /* ---- UNLINK ---- */
    case SYS_UNLINK: {
        char path[256];
        if (resolve_user_path((const char *)r->rdi, path, 256) < 0) { r->rax = -1; break; }
        r->rax = (uint64_t)(vfs_unlink(path) < 0 ? -1 : 0);
        break;
    }

    /* ---- STAT ---- */
    case SYS_STAT: {
        char path[256];
        if (resolve_user_path((const char *)r->rdi, path, 256) < 0) { r->rax = -1; break; }
        if (r->rsi && !validate_user_ptr((void *)r->rsi, sizeof(uint32_t))) { r->rax = -1; break; }
        if (r->rdx && !validate_user_ptr((void *)r->rdx, sizeof(uint8_t))) { r->rax = -1; break; }
        uint32_t *size_out = (uint32_t *)r->rsi;
        uint8_t *flags_out = (uint8_t *)r->rdx;
        uint32_t size;
        uint8_t flags;
        if (vfs_stat(path, &size, &flags) < 0) {
            r->rax = -1;
        } else {
            if (size_out) *size_out = size;
            if (flags_out) *flags_out = flags;
            r->rax = size;
        }
        break;
    }

    /* ---- PIPE ---- */
    case SYS_PIPE: {
        if (!validate_user_ptr((void *)r->rdi, 2 * sizeof(int))) { r->rax = -1; break; }
        int *fds = (int *)r->rdi;
        pipe_t *p = pipe_alloc();
        if (!p) { r->rax = -1; break; }
        if (!cur) { p->used = 0; r->rax = -1; break; }
        int rfd = task_fd_alloc(cur, p, FD_TYPE_PIPE_R);
        int wfd = task_fd_alloc(cur, p, FD_TYPE_PIPE_W);
        if (rfd < 0 || wfd < 0) {
            if (rfd >= 0) task_fd_free(cur, rfd);
            if (wfd >= 0) task_fd_free(cur, wfd);
            p->used = 0;
            r->rax = -1;
            break;
        }
        fds[0] = rfd;
        fds[1] = wfd;
        r->rax = 0;
        break;
    }

    /* ---- FORK ---- */
    case SYS_FORK: {
        if (!cur) { r->rax = -1; break; }
        /* Clone kernel page tables */
        uint64_t child_pml4 = paging_clone_current();
        if (!child_pml4) { r->rax = -1; break; }

        /* Create child task struct */
        task_struct_t *child = (task_struct_t *)pmm_alloc();
        if (!child) { r->rax = -1; break; }

        /* Copy task struct */
        for (size_t i = 0; i < sizeof(task_struct_t); i++)
            ((uint8_t *)child)[i] = ((uint8_t *)cur)[i];

        /* Allocate new kernel stack for child */
        uint64_t *child_stack = (uint64_t *)pmm_alloc();
        if (!child_stack) { pmm_free(child); r->rax = -1; break; }

        /* Copy kernel stack content */
        uint64_t stack_offset = cur->kernel_stack_base;
        uint64_t child_stack_base = (uint64_t)child_stack;
        for (uint64_t off = 0; off < TASK_STACK_SIZE; off += 8) {
            *(uint64_t *)(child_stack_base + off) = *(uint64_t *)(stack_offset + off);
        }

        /* Calculate child RSP relative to new stack */
        uint64_t rsp_delta = cur->rsp - cur->kernel_stack_base;
        child->rsp = child_stack_base + rsp_delta;
        child->kernel_stack_base = child_stack_base;
        child->cr3 = child_pml4;
        child->ppid = cur->pid;

        /* Assign new PID */
        child->pid = 0; /* will be set by task_create flow */

        /* Copy FD table */
        for (int i = 0; i < TASK_MAX_FD; i++) {
            if (cur->fd[i].used && cur->fd[i].type == FD_TYPE_FILE) {
                file_t *f = (file_t *)cur->fd[i].ptr;
                /* Create new file handle for child */
                file_t *new_f = (file_t *)kmalloc(sizeof(file_t));
                if (new_f) {
                    new_f->node = f->node;
                    new_f->offset = f->offset;
                    child->fd[i].ptr = new_f;
                } else {
                    child->fd[i].used = 0;
                }
            }
        }

        /* Insert child into scheduler list */
        if (cur) {
            child->next = cur->next;
            cur->next = child;
        }

        r->rax = child->pid;  /* parent gets child PID */
        break;
    }

    /* ---- WAIT ---- */
    case SYS_WAIT: {
        if (!cur) { r->rax = -1; break; }
        /* Find a terminated child */
        task_struct_t *scan = cur->next;
        while (scan != cur) {
            if (scan->ppid == cur->pid && scan->state == TASK_TERMINATED) {
                int code = scan->exit_code;
                task_destroy(scan);
                r->rax = (uint64_t)code;
                break;
            }
            scan = scan->next;
        }
        if (scan == cur) {
            /* No terminated child found — block until one exists */
            /* Simple polling for now */
            r->rax = -1;
        }
        break;
    }

    /* ---- GETPID ---- */
    case SYS_GETPID:
        r->rax = cur ? cur->pid : 0;
        break;

    /* ---- GETPPID ---- */
    case SYS_GETPPID:
        r->rax = cur ? cur->ppid : 0;
        break;

    /* ---- KILL ---- */
    case SYS_KILL: {
        uint32_t pid = (uint32_t)r->rdi;
        int sig = (int)r->rsi;
        task_struct_t *target = task_find(pid);
        if (!target) { r->rax = -1; break; }
        if (sig == SIGKILL) {
            target->state = TASK_TERMINATED;
            target->exit_code = -SIGKILL;
        } else if (sig >= 0 && sig < TASK_MAX_SIGNALS) {
            target->signals[sig].pending = 1;
        }
        r->rax = 0;
        break;
    }

    /* ---- SIGNAL ---- */
    case SYS_SIGNAL: {
        int sig = (int)r->rdi;
        void (*handler)(int) = (void (*)(int))r->rsi;
        if (!cur || sig < 0 || sig >= TASK_MAX_SIGNALS) { r->rax = -1; break; }
        cur->signals[sig].handler = handler;
        r->rax = 0;
        break;
    }

    /* ---- BRK ---- */
    case SYS_BRK: {
        /* Simple brk: if addr=0, return current break; else set break */
        /* For now, just return success (dynamic allocation not fully supported yet) */
        r->rax = 0;
        break;
    }

    /* ---- DUP2 ---- */
    case SYS_DUP2: {
        int oldfd = (int)r->rdi;
        int newfd = (int)r->rsi;
        if (!cur) { r->rax = -1; break; }
        if (oldfd < 0 || oldfd >= TASK_MAX_FD || !cur->fd[oldfd].used) { r->rax = -1; break; }
        if (newfd < 0 || newfd >= TASK_MAX_FD) { r->rax = -1; break; }
        if (cur->fd[newfd].used) {
            /* Close existing fd first */
            if (cur->fd[newfd].type == FD_TYPE_FILE)
                vfs_close((file_t *)cur->fd[newfd].ptr);
        }
        cur->fd[newfd] = cur->fd[oldfd];
        r->rax = newfd;
        break;
    }

    /* ---- CHDIR ---- */
    case SYS_CHDIR: {
        /* Just store the path — simple implementation */
        r->rax = 0;
        break;
    }

    /* ---- GETCWD ---- */
    case SYS_GETCWD: {
        if (!validate_user_ptr((void *)r->rdi, r->rsi)) { r->rax = 0; break; }
        char *buf = (char *)r->rdi;
        uint64_t cap = r->rsi;
        buf[0] = '/';
        if (cap > 1) buf[1] = 0;
        r->rax = 0;
        break;
    }

    /* ---- REBOOT ---- */
    case SYS_REBOOT: {
        uint64_t cmd = r->rdi;
        if (cmd == REBOOT_CMD_SHUTDOWN) {
            gpu_print("[syscall] shutting down...\n");
            serial_print("[syscall] shutdown via ACPI (QEMU)\n");
            /* ACPI shutdown: write to QEMU-specific port */
            /* For QEMU: outw(0x604, 0x2000) */
            __asm__ volatile(
                "movw $0x2000, %%ax\n"
                "movw $0x604, %%dx\n"
                "outw %%ax, %%dx\n"
                ::: "ax", "dx"
            );
            __asm__ volatile("cli; hlt");
        } else if (cmd == REBOOT_CMD_REBOOT) {
            gpu_print("[syscall] rebooting...\n");
            /* Triple fault via IDT reset */
            __asm__ volatile("lidt (%%rax)" : : "a"(0));
            __asm__ volatile("int $3");
        }
        r->rax = 0;
        break;
    }

    default:
        serial_print("[syscall] unknown: ");
        serial_print_hex(r->rax);
        serial_print("\n");
        r->rax = -1;
        break;
    }
}
