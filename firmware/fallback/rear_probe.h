#ifndef CRTC_REAR_PROBE_H
#define CRTC_REAR_PROBE_H
#include <stdint.h>
/* Explicit local ST-Link one-shot; no boot motion or renewable run lease.
 * Only the rear pair (indices 2/3) is selected by the board owner below. */
#define REAR_PROBE_KEY 0x52520000UL
#define REAR_PROBE_LIMIT_MS 500U
#define REAR_PROBE_PWM (-1000)
typedef struct {
    uint32_t last_command, started;
    uint8_t state; /* 0 normal, 1 run, 2 done, 3 stopped/fault */
} rear_probe_t;
static inline int rear_probe_poll(rear_probe_t *s, uint32_t command,
                                  uint8_t healthy, uint8_t stop, uint32_t now)
{
    if (!command) { s->state = 0; return -1; }
    if (stop || !healthy || (command & 0xffff0000UL) != REAR_PROBE_KEY) {
        s->last_command = command;
        s->state = 3;
        return 0;
    }
    if (!s->state) {
        if (command == s->last_command) { s->state = 3; return 0; }
        s->last_command = command;
        s->started = now;
        s->state = 1;
    }
    if (s->state == 1 && now - s->started >= REAR_PROBE_LIMIT_MS)
        s->state = 2;
    return s->state == 1;
}
#endif
