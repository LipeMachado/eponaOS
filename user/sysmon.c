#include "libc.h"
#include "stdlib.h"

static void print_bar(int percent, int width) {
    putchar('[');
    int filled = (percent * width) / 100;
    if (filled > width) filled = width;
    for (int i = 0; i < width; i++) {
        putchar(i < filled ? '#' : '-');
    }
    putchar(']');
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    puts("============================================\n");
    puts("       EponaOS System Monitor v1.0\n");
    puts("============================================\n\n");

    int pid = sys_getpid();
    int ppid = sys_getppid();

    char buf[32];

    printf("  Process ID:   ");
    itoa(pid, buf); puts(buf); putchar('\n');

    printf("  Parent PID:   ");
    itoa(ppid, buf); puts(buf); putchar('\n');

    /* Memory info */
    uint64_t cur_break = sys_brk(0);
    puts("  Heap break:   0x");
    itox((uint32_t)cur_break, buf); puts(buf); putchar('\n');

    uint32_t heap_kb = (uint32_t)(cur_break >> 10);
    printf("  Heap used:    ");
    utoa(heap_kb, buf); puts(buf); puts(" KiB\n");

    /* Memory bar (assuming 512 MiB total) */
    uint32_t total_kb = 512 * 1024;
    uint32_t used_pct = (heap_kb * 100) / total_kb;
    puts("  Memory:       ");
    print_bar(used_pct > 100 ? 100 : used_pct, 20);
    putchar(' ');
    utoa(used_pct, buf); puts(buf); puts("%%\n");

    puts("\n--------------------------------------------\n");

    /* Run for ~5 seconds, updating uptime each second */
    puts("  Monitoring for 5 seconds...\n\n");

    for (int sec = 0; sec < 5; sec++) {
        printf("  [%d sec] PID=", sec);
        itoa(pid, buf); puts(buf);

        /* Busy wait ~1 second (no timer syscall) */
        for (volatile uint64_t i = 0; i < 50000000ULL; i++) {
            __asm__ volatile("pause");
        }

        /* Update heap info */
        cur_break = sys_brk(0);
        heap_kb = (uint32_t)(cur_break >> 10);
        printf("  heap=");
        utoa(heap_kb, buf); puts(buf);
        puts(" KiB  ");
        print_bar(heap_kb > total_kb ? 100 : (heap_kb * 100) / total_kb, 15);
        putchar('\n');
    }

    puts("\n============================================\n");
    puts("  Monitor complete. Exiting.\n");
    puts("============================================\n");

    return 0;
}
