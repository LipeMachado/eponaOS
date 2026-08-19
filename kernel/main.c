/* kernel/main.c — nucleo do EponaOS */
#include "serial.h"
#include "gpu.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "pci.h"
#include "ata.h"
#include "mouse.h"
#include "string.h"
#include "vfs.h"
#include "fat.h"
#include "rtl8139.h"
#include "net.h"
#include "scheduler.h"
#include "shell.h"
#include "syscall.h"
#include "elf.h"
#include "acpi.h"
#include "rtc.h"


static void print_u64(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0)
        buf[--i] = '0';
    while (v) {
        buf[--i] = (char) ('0' + v % 10);
        v /= 10;
    }
    gpu_print(&buf[i]);
    serial_print(&buf[i]);
}

static void print_ip(uint32_t ip) {
    char buf[4];
    for (int part = 0; part < 4; part++) {
        uint8_t n = (uint8_t) ((ip >> (part * 8)) & 0xFF);
        int i = 0;
        if (n >= 100)
            buf[i++] = (char) ('0' + n / 100);
        if (n >= 10)
            buf[i++] = (char) ('0' + (n / 10) % 10);
        buf[i++] = (char) ('0' + n % 10);
        buf[i] = 0;
        gpu_print(buf);
        if (part < 3)
            gpu_print(".");
    }
}

static void print_preview(const char *s, int max) {
    for (int i = 0; s[i] && i < max; i++) {
        char c = s[i];
        if (c == '\r' || c == '\n' || c == '\t')
            gpu_print(" ");
        else
            gpu_putc(c);
    }
}

/* Tasks de demonstracao da multitarefa preemptiva */
void task_a(void) {
    __asm__ volatile("sti");
    uint32_t ticks = 0;
    while (1) {
        if (++ticks == 1000000) {
            ticks = 0;
        }
    }
}

void task_b(void) {
    __asm__ volatile("sti");
    uint32_t ticks = 0;
    while (1) {
        if (++ticks == 1000000) {
            ticks = 0;
        }
    }
}

void task_net(void) {
    __asm__ volatile("sti");
    while (1) {
        net_poll();
    }
}

void kernel_main(void) {
    serial_init();
    gpu_init();
    gdt_init();
    idt_init();
    pic_remap();
    pit_init(100);
    pmm_init();
    paging_init();
    heap_init();
    gpu_init_framebuffer();
    pci_enumerate();
    mouse_init();
    serial_print("[main] calling rtc_init...\n");
    rtc_init();
    serial_print("[main] calling acpi_init...\n");
    acpi_init();
    serial_print("[main] acpi_init done\n");

    {
        uint16_t ident[256];
        int bus = 0;
        if (ata_drive_present(bus)) {
            serial_print("[ata] primary bus present, identifying master...\n");
            if (ata_identify(bus, 1, ident) == 0) {
                serial_print("[ata] master drive identified.\n");
                serial_print("[ata] signature: ");
                serial_print_hex(ident[0]);
                serial_print("\n");
                uint32_t sectors = (uint32_t) ident[60] | ((uint32_t) ident[61] << 16);
                serial_print("[ata] size: ");
                serial_print_dec(sectors / 2 / 1024);
                serial_print(" MiB\n");
            } else {
                serial_print("[ata] no master drive\n");
            }
        } else {
            serial_print("[ata] no primary bus\n");
        }
    }

    vfs_init();

    {
        vfs_filesystem_t *fat = fat_mount(0, 0, 0);
        if (fat) {
            vfs_mount("/", fat);
            serial_print("[main] FAT mounted on /\n");

            serial_print("[main] Root directory:\n");

            vfs_readdir("/", NULL, NULL);

            vfs_node_t *child = fat->root->children;
            while (child) {
                serial_print("  ");
                serial_print(child->name);
                serial_print("\n");
                child = child->next;
            }

            file_t *f = vfs_open("/hello.txt");
            if (f) {
                serial_print("[main] opened file\n");
                char buf[64];
                int n = vfs_read(f, 63, buf);
                if (n > 0) {
                    buf[n] = 0;
                    serial_print("[main] content: \"");
                    serial_print(buf);
                    serial_print("\"\n");
                }
                vfs_close(f);
            } else {
                serial_print("[main] could not open file\n");
            }
        } else {
            serial_print("[main] slave: no FAT\n");

            fat = fat_mount(0, 1, 0);
            if (fat) {
                vfs_mount("/", fat);
                serial_print("[main] FAT mounted on / (master)\n");
            } else {
                serial_print("[main] no FAT on either drive\n");
            }
        }
    }

    net_init();
    for (int i = 0; i < 5000000; i++)
        net_poll();

    scheduler_init();

    gpu_set_color(0x0B, 0x00);
    gpu_print("=== EponaOS ===\n");
    gpu_set_color(0x0F, 0x00);
    gpu_print("Kernel x86_64 long mode\n");
    gpu_print("RAM: ");
    print_u64(pmm_total_bytes() / (1024 * 1024));
    gpu_print(" MiB\n");

    /* GPU detection */
    const gpu_info_t *gpu = pci_find_gpu();
    if (gpu) {
        gpu_set_color(0x0A, 0x00);
        gpu_print("GPU: ");
        if (gpu->is_intel) gpu_print("Intel");
        else if (gpu->is_nvidia) gpu_print("NVIDIA");
        else if (gpu->is_amd) gpu_print("AMD");
        else gpu_print("Unknown");
        gpu_print(" (BAR0=");
        char hex[12];
        int h = 10; hex[h] = 0;
        uint32_t v = gpu->bar0;
        if (v == 0) hex[--h] = '0';
        while (v) { hex[--h] = "0123456789ABCDEF"[v & 0xF]; v >>= 4; }
        gpu_print(&hex[h]);
        gpu_print(")\n");
    } else {
        gpu_set_color(0x0E, 0x00);
        gpu_print("GPU: VBE framebuffer\n");
    }

    /* ACPI status */
    gpu_set_color(0x0A, 0x00);
    gpu_print("ACPI: ");
    gpu_print(acpi_get_pm1a() ? "OK" : "not found");
    gpu_print("\n");

    /* RTC date/time */
    rtc_time_t t;
    rtc_read_time(&t);
    gpu_set_color(0x0F, 0x00);
    gpu_print("Date: ");
    print_u64(t.day); gpu_print("/"); print_u64(t.month); gpu_print("/"); print_u64(t.year);
    gpu_print("  Time: ");
    print_u64(t.hours); gpu_print(":");
    if (t.minutes < 10) gpu_print("0");
    print_u64(t.minutes); gpu_print(":");
    if (t.seconds < 10) gpu_print("0");
    print_u64(t.seconds);
    gpu_print("\n");

    if (net_is_configured()) {
        gpu_set_color(0x0A, 0x00);
        gpu_print("NET: ");
        print_ip(net_local_ip());
        gpu_print("\n");
    } else {
        gpu_set_color(0x0C, 0x00);
        gpu_print("NET: offline\n");
    }

    if (net_dns_answer_ip()) {
        gpu_set_color(0x0A, 0x00);
        gpu_print("DNS: example.com = ");
        print_ip(net_dns_answer_ip());
        gpu_print("\n");
    }

    gpu_set_color(net_http_ok() ? 0x0A : 0x0C, 0x00);
    gpu_print(net_http_ok() ? "HTTP: GET / OK\n" : "HTTP: sem resposta\n");
    if (net_http_body_len() > 0) {
        gpu_set_color(0x0E, 0x00);
        gpu_print("HTML: ");
        print_preview(net_http_body_preview(), 66);
        gpu_print("\n");
    }
    gpu_set_color(0x0F, 0x00);

    task_create(task_a);
    task_create(task_b);
    task_create(task_net);

    __asm__ volatile("sti");
    serial_print("[kernel] multitarefa ativa.\n");
    gpu_print("Digite 'help' para comandos.\n\n");

    shell_run();

    schedule();

    for (;;) {
        __asm__ volatile("hlt");
    }
}
