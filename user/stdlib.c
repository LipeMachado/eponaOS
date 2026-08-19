#include "libc.h"
#include "stdlib.h"

/* ================================================================
   String functions
   ================================================================ */

int strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n && a[i] && b[i]; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

void strcpy(char *dst, const char *src) {
    while ((*dst++ = *src++)) {}
}

void strncpy(char *dst, const char *src, int n) {
    int i = 0;
    for (i = 0; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
}

void strcat(char *dst, const char *src) {
    while (*dst) dst++;
    while ((*dst++ = *src++)) {}
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : (void *)0;
}

int starts_with(const char *s, const char *prefix) {
    while (*prefix) { if (*s++ != *prefix++) return 0; }
    return 1;
}

int ends_with(const char *s, const char *suffix) {
    int sl = strlen(s);
    int tl = strlen(suffix);
    if (tl > sl) return 0;
    return strcmp(s + sl - tl, suffix) == 0;
}

void reverse_str(char *s) {
    int i = 0, j = strlen(s) - 1;
    while (i < j) {
        char tmp = s[i];
        s[i] = s[j];
        s[j] = tmp;
        i++; j--;
    }
}

/* ================================================================
   Number conversion
   ================================================================ */

void itoa(int val, char *buf) {
    int i = 0;
    int neg = 0;
    uint32_t uval;

    if (val < 0) { neg = 1; uval = (uint32_t)(-(val + 1)) + 1; }
    else { uval = (uint32_t)val; }

    if (uval == 0) { buf[i++] = '0'; }
    else {
        while (uval) { buf[i++] = (char)('0' + uval % 10); uval /= 10; }
    }
    if (neg) buf[i++] = '-';
    buf[i] = 0;
    reverse_str(buf);
}

void utoa(uint32_t val, char *buf) {
    int i = 0;
    if (val == 0) { buf[i++] = '0'; }
    else {
        while (val) { buf[i++] = (char)('0' + val % 10); val /= 10; }
    }
    buf[i] = 0;
    reverse_str(buf);
}

void itox(uint32_t val, char *buf) {
    const char *hex = "0123456789abcdef";
    int i = 0;
    if (val == 0) { buf[i++] = '0'; }
    else {
        while (val) { buf[i++] = hex[val & 0xf]; val >>= 4; }
    }
    buf[i] = 0;
    reverse_str(buf);
}

int atoi(const char *s) {
    int neg = 0, val = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
    return neg ? -val : val;
}

/* ================================================================
   I/O helpers
   ================================================================ */

void putchar(char c) {
    sys_write(STDOUT_FILENO, &c, 1);
}

void puts(const char *s) {
    sys_write(STDOUT_FILENO, s, (uint64_t)strlen(s));
}

int getchar(void) {
    char c;
    if (sys_read(STDIN_FILENO, &c, 1) == 1) return c;
    return -1;
}

/* ================================================================
   printf / sprintf helpers
   ================================================================ */

static void _put_char_to(char **out, char c) {
    if (out) { **out = c; (*out)++; }
    else { putchar(c); }
}

static void _put_str_to(char **out, const char *s) {
    while (*s) _put_char_to(out, *s++);
}

static void _put_int_to(char **out, int val) {
    char buf[16];
    itoa(val, buf);
    _put_str_to(out, buf);
}

static void _put_uint_to(char **out, uint32_t val) {
    char buf[16];
    utoa(val, buf);
    _put_str_to(out, buf);
}

static void _put_hex_to(char **out, uint32_t val) {
    char buf[16];
    itox(val, buf);
    _put_str_to(out, buf);
}

static void _put_ptr_to(char **out, void *ptr) {
    uint64_t val = (uint64_t)ptr;
    char buf[20];
    const char *hex = "0123456789abcdef";
    int i = 0;
    if (val == 0) { _put_str_to(out, "0x0"); return; }
    _put_str_to(out, "0x");
    while (val) { buf[i++] = hex[val & 0xf]; val >>= 4; }
    buf[i] = 0;
    reverse_str(buf);
    _put_str_to(out, buf);
}

static void _vprintf(char **out, const char *fmt, __builtin_va_list ap) {
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 'd') _put_int_to(out, __builtin_va_arg(ap, int));
            else if (*fmt == 'u') _put_uint_to(out, __builtin_va_arg(ap, uint32_t));
            else if (*fmt == 'x' || *fmt == 'X') _put_hex_to(out, __builtin_va_arg(ap, uint32_t));
            else if (*fmt == 's') _put_str_to(out, __builtin_va_arg(ap, char *));
            else if (*fmt == 'c') _put_char_to(out, (char)__builtin_va_arg(ap, int));
            else if (*fmt == 'p') _put_ptr_to(out, __builtin_va_arg(ap, void *));
            else if (*fmt == '%') _put_char_to(out, '%');
            else { _put_char_to(out, '%'); _put_char_to(out, *fmt); }
            fmt++;
        } else {
            _put_char_to(out, *fmt++);
        }
    }
}

/* ================================================================
   printf / sprintf
   ================================================================ */

void printf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    _vprintf((void *)0, fmt, ap);
    __builtin_va_end(ap);
}

void sprintf(char *buf, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    _vprintf(&buf, fmt, ap);
    *buf = 0;
    __builtin_va_end(ap);
}

/* ================================================================
   Heap allocator (linked-list, grows via sys_brk)
   ================================================================ */

#define HEAP_SIZE   0x10000  /* 64 KiB static heap region */

typedef struct block {
    uint32_t size;       /* usable bytes (excluding header) */
    int free;            /* 1 = free, 0 = allocated */
    struct block *next;
} block_t;

static block_t *heap_base = (void *)0;
static char *heap_end = (void *)0;
static int heap_inited = 0;

static void heap_init(void) {
    if (heap_inited) return;
    heap_inited = 1;

    uint64_t cur_break = sys_brk(0);
    uint64_t new_break = cur_break + HEAP_SIZE;
    sys_brk(new_break);

    heap_base = (block_t *)cur_break;
    heap_base->size = HEAP_SIZE - sizeof(block_t);
    heap_base->free = 1;
    heap_base->next = (void *)0;
    heap_end = (char *)new_break;
}

static block_t *find_free(uint32_t size) {
    block_t *cur = heap_base;
    while (cur) {
        if (cur->free && cur->size >= size) return cur;
        cur = cur->next;
    }
    return (void *)0;
}

static block_t *split_block(block_t *blk, uint32_t size) {
    if (blk->size >= size + sizeof(block_t) + 16) {
        block_t *new_blk = (block_t *)((char *)blk + sizeof(block_t) + size);
        new_blk->size = blk->size - size - sizeof(block_t);
        new_blk->free = 1;
        new_blk->next = blk->next;
        blk->next = new_blk;
        blk->size = size;
    }
    return blk;
}

void *malloc(uint32_t size) {
    if (size == 0) return (void *)0;
    heap_init();

    block_t *blk = find_free(size);
    if (!blk) {
        /* Grow heap */
        uint64_t cur_break = sys_brk(0);
        uint64_t need = sizeof(block_t) + size;
        uint64_t grow = (need > HEAP_SIZE) ? need : HEAP_SIZE;
        sys_brk(cur_break + grow);
        heap_end = (char *)(cur_break + grow);

        /* Try to coalesce with last block */
        block_t *last = heap_base;
        while (last && last->next) last = last->next;
        if (last && last->free) {
            last->size += grow;
            blk = last;
        } else {
            blk = (block_t *)cur_break;
            blk->size = grow - sizeof(block_t);
            blk->free = 1;
            blk->next = (void *)0;
            if (last) last->next = blk;
            else heap_base = blk;
        }
    }

    blk = split_block(blk, size);
    blk->free = 0;
    return (char *)blk + sizeof(block_t);
}

void free(void *ptr) {
    if (!ptr) return;
    block_t *blk = (block_t *)((char *)ptr - sizeof(block_t));
    blk->free = 1;

    /* Coalesce adjacent free blocks */
    block_t *cur = heap_base;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            cur->size += sizeof(block_t) + cur->next->size;
            cur->next = cur->next->next;
        } else {
            cur = cur->next;
        }
    }
}

void *calloc(uint32_t count, uint32_t size) {
    uint32_t total = count * size;
    void *p = malloc(total);
    if (p) {
        char *c = (char *)p;
        for (uint32_t i = 0; i < total; i++) c[i] = 0;
    }
    return p;
}

/* ================================================================
   Process wrappers
   ================================================================ */

void exit(int code) {
    sys_exit(code);
    __builtin_unreachable();
}

int fork(void) {
    return sys_fork();
}

void wait(int *status) {
    sys_wait(status);
}
