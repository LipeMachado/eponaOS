#ifndef EPONA_RTC_H
#define EPONA_RTC_H

#include <stdint.h>

typedef struct {
    uint8_t seconds, minutes, hours;
    uint8_t day, month;
    uint16_t year;
    uint8_t day_of_week;
} rtc_time_t;

void rtc_read_time(rtc_time_t *t);
void rtc_init(void);

#endif
