#include "rtc.h"
#include "io.h"
#include "serial.h"
#include <stdint.h>

static inline uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

void rtc_read_time(rtc_time_t *t) {
    uint8_t reg_b = cmos_read(0x0B);
    int bcd = !(reg_b & 0x04);

    t->seconds = cmos_read(0x00);
    t->minutes = cmos_read(0x02);
    t->hours = cmos_read(0x04);
    t->day_of_week = cmos_read(0x06);
    t->day = cmos_read(0x07);
    t->month = cmos_read(0x08);
    t->year = cmos_read(0x09);

    if (bcd) {
        t->seconds = bcd_to_bin(t->seconds);
        t->minutes = bcd_to_bin(t->minutes);
        t->hours = bcd_to_bin(t->hours);
        t->day = bcd_to_bin(t->day);
        t->month = bcd_to_bin(t->month);
        t->year = bcd_to_bin(t->year);
        t->day_of_week = bcd_to_bin(t->day_of_week);
    }

    if (!(reg_b & 0x02) && (t->hours & 0x80))
        t->hours = (t->hours & 0x7F) + 12;
    t->hours &= ~0x80;

    uint8_t century = cmos_read(0x32);
    if (century)
        century = bcd ? bcd_to_bin(century) : century;
    else
        century = 20;

    t->year = (uint16_t)(century * 100) + t->year;
}

void rtc_init(void) {
    serial_print("[rtc] initialized\n");
}
