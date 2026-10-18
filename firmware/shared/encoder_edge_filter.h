#ifndef CRTC_ENCODER_EDGE_FILTER_H
#define CRTC_ENCODER_EDGE_FILTER_H
#include <stdint.h>

/* A-only x2 quadrature decoder. Qualify the A pulse width before counting;
 * capture B at the A edge, not at the later owner-loop poll. All calls must
 * be serialized. Time is an unsigned wrapping hardware counter. */
typedef struct {
    uint32_t count, since, minimum_ticks;
    uint32_t accepted, short_pulses, duplicate_irqs;
    uint8_t stable_a, candidate_a, candidate_b, pending;
} encoder_edge_filter_t;
void encoder_edge_filter_init(encoder_edge_filter_t *s, uint8_t a,
                              uint32_t minimum_ticks);
void encoder_edge_filter_irq(encoder_edge_filter_t *s, uint32_t now,
                             uint8_t a, uint8_t b);
void encoder_edge_filter_poll(encoder_edge_filter_t *s, uint32_t now,
                              uint8_t a, uint8_t b);
typedef struct {
    uint32_t start, window_ticks;
    uint8_t used, limit, latched;
} encoder_irq_budget_t;
void encoder_irq_budget_init(encoder_irq_budget_t *s, uint32_t now,
                              uint32_t window_ticks, uint8_t limit);
int encoder_irq_budget_allow(encoder_irq_budget_t *s, uint32_t now);
#endif
