#pragma once
#include <stdint.h>
#include <stdbool.h>

#define ALARM_HISTORY_SIZE 20

typedef struct {
    uint32_t ts_start;
    uint32_t ts_end;
    uint8_t  code;
    uint8_t  reserved[3];
} alarm_event_t;

void alarm_history_init(void);
void alarm_history_on_code_change(uint8_t new_code, uint32_t now_epoch);
char *alarm_history_dump_json(void);
uint16_t alarm_history_count(void);
