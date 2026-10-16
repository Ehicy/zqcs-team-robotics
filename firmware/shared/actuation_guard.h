#ifndef CRTC_ACTUATION_GUARD_H
#define CRTC_ACTUATION_GUARD_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ACTUATION_MODE_HOLD_MS 50U
typedef struct { uint32_t stopped_ms; uint8_t last_mode, active; } actuation_guard_t;
/* Record ACTUAL nonzero output, retaining history across session changes. */
void actuation_guard_record(actuation_guard_t *guard,uint8_t mode,uint32_t now);
/* Call AFTER hardware stop. Repeated stopped updates never move the deadline. */
void actuation_guard_stop(actuation_guard_t *guard,uint32_t now);
int actuation_guard_allows(const actuation_guard_t *guard,uint8_t mode,uint32_t now);
int actuation_guard_idle(const actuation_guard_t *guard,uint32_t now);
#ifdef __cplusplus
}
#endif
#endif
