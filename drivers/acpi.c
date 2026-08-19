#include "acpi.h"
#include "io.h"
#include "serial.h"
#include "string.h"
#include "paging.h"
#include <stdint.h>

static uint32_t pm1a_cnt = 0;
static uint32_t pm1b_cnt = 0;

static uint8_t acpi_checksum(const uint8_t *data, uint32_t len) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++)
        sum += data[i];
    return sum;
}

static uint32_t find_rsdp_in_region(uint64_t base, uint32_t len) {
    const volatile uint8_t *mem = (const volatile uint8_t *) base;
    for (uint32_t i = 0; i < len; i += 16) {
        if (memcmp((const void *)&mem[i], ACPI_RSDP_SIG, 8) == 0) {
            if (acpi_checksum((const uint8_t *)&mem[i], 20) == 0)
                return (uint32_t)(base + i);
        }
    }
    return 0;
}

static uint32_t find_rsdp(void) {
    /* Read EBDA segment from BIOS Data Area */
    volatile uint16_t *ebda_ptr = (volatile uint16_t *)0x40E;
    uint16_t ebda_seg = *ebda_ptr;
    uint32_t ebda = (uint32_t)ebda_seg << 4;
    if (ebda && ebda < 0xA0000) {
        uint32_t addr = find_rsdp_in_region(ebda, 0x400);
        if (addr) return addr;
    }
    return find_rsdp_in_region(0xE0000, 0x20000);
}

static uint32_t find_fadt(uint32_t rsdt_addr) {
    /* Validate RSDT address: must be below 1MB (accessible in low memory) */
    if (!rsdt_addr || rsdt_addr >= 0x100000) {
        serial_print("[acpi] RSDT address invalid or too high: 0x");
        serial_print_hex(rsdt_addr);
        serial_print("\n");
        return 0;
    }
    volatile uint32_t *rsdt_ptr = (volatile uint32_t *)(uint64_t)rsdt_addr;
    uint32_t length = rsdt_ptr[1];
    if (length < 36 || length > 0x10000) {
        serial_print("[acpi] RSDT length invalid: ");
        serial_print_dec(length);
        serial_print("\n");
        return 0;
    }
    uint32_t count = (length - 36) / 4;
    if (count > 128) count = 128;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t entry_addr = rsdt_ptr[9 + i];
        /* Validate each entry address */
        if (!entry_addr || entry_addr >= 0x100000) continue;
        volatile uint8_t *sig = (volatile uint8_t *)(uint64_t)entry_addr;
        if (memcmp((const void *)sig, ACPI_FADT_SIG, 4) == 0)
            return entry_addr;
    }
    return 0;
}

void acpi_init(void) {
    serial_print("[acpi] searching for RSDP...\n");

    uint32_t rsdp_addr = find_rsdp();
    if (!rsdp_addr) {
        serial_print("[acpi] RSDP not found\n");
        return;
    }
    serial_print("[acpi] RSDP at 0x");
    serial_print_hex(rsdp_addr);
    serial_print("\n");

    volatile uint8_t *rsdp = (volatile uint8_t *)(uint64_t)rsdp_addr;
    serial_print("[acpi] OEM: ");
    for (int i = 0; i < 6; i++)
        serial_putc((char)rsdp[9 + i]);
    serial_print("\n");

    /* RSDT address at offset 16 */
    uint32_t rsdt_addr = *(volatile uint32_t *)(uint64_t)(rsdp_addr + 16);
    serial_print("[acpi] RSDT at 0x");
    serial_print_hex(rsdt_addr);
    serial_print("\n");

    if (!rsdt_addr) {
        serial_print("[acpi] RSDT address is null\n");
        return;
    }

    uint32_t fadt_addr = find_fadt(rsdt_addr);
    if (!fadt_addr) {
        serial_print("[acpi] FADT not found (ACPI tables incomplete in QEMU)\n");
        return;
    }
    serial_print("[acpi] FADT at 0x");
    serial_print_hex(fadt_addr);
    serial_print("\n");

    /* FADT: PM1a_CNT_BLK at offset 52, PM1b_CNT_BLK at offset 56 */
    volatile uint32_t *fadt = (volatile uint32_t *)(uint64_t)fadt_addr;
    pm1a_cnt = fadt[13]; /* offset 52 / 4 */
    pm1b_cnt = fadt[14]; /* offset 56 / 4 */

    serial_print("[acpi] PM1a_CNT_BLK: 0x");
    serial_print_hex(pm1a_cnt);
    serial_print("\n");
    serial_print("[acpi] PM1b_CNT_BLK: 0x");
    serial_print_hex(pm1b_cnt);
    serial_print("\n");
    serial_print("[acpi] init done\n");
}

uint32_t acpi_get_pm1a(void) {
    return pm1a_cnt;
}

uint32_t acpi_get_pm1b(void) {
    return pm1b_cnt;
}

void acpi_shutdown(void) {
    serial_print("[acpi] shutdown via ACPI S5\n");
    if (pm1a_cnt) {
        serial_print("[acpi] writing S5 to PM1a\n");
        outw((uint16_t)pm1a_cnt, 0x2000);
    }
    if (pm1b_cnt) {
        outw((uint16_t)pm1b_cnt, 0x2000);
    }
    /* Fallback: QEMU Bochs shutdown port */
    serial_print("[acpi] fallback: QEMU shutdown port 0x604\n");
    outw(0x604, 0x2000);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void acpi_reboot(void) {
    serial_print("[acpi] reboot via keyboard controller\n");
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inb(0x64);
    outb(0x64, 0xFE);
    for (;;) {
        __asm__ volatile("hlt");
    }
}
