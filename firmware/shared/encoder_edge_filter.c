#include "encoder_edge_filter.h"
#include <string.h>

void encoder_irq_budget_init(encoder_irq_budget_t *s, uint32_t now,
                              uint32_t window_ticks, uint8_t limit)
{
    memset(s,0,sizeof(*s)); s->start=now; s->window_ticks=window_ticks;
    s->limit=limit; s->latched=(uint8_t)(!window_ticks || !limit);
}
int encoder_irq_budget_allow(encoder_irq_budget_t *s, uint32_t now)
{
    if(s->latched) return 0;
    if((uint32_t)(now-s->start)>=s->window_ticks) { s->start=now; s->used=0; }
    if(s->used>=s->limit) { s->latched=1; return 0; }
    s->used++; return 1;
}

static void accept(encoder_edge_filter_t *s)
{
    s->count += s->candidate_a != s->candidate_b ? 1U : UINT32_MAX;
    s->stable_a=s->candidate_a;
    s->pending=0;
    s->accepted++;
}
static void candidate(encoder_edge_filter_t *s, uint32_t now, uint8_t a, uint8_t b)
{
    if(a!=s->stable_a) {
        s->candidate_a=a; s->candidate_b=b; s->since=now; s->pending=1;
    }
}
void encoder_edge_filter_init(encoder_edge_filter_t *s, uint8_t a, uint32_t minimum_ticks)
{
    memset(s,0,sizeof(*s)); s->stable_a=(uint8_t)!!a;
    s->minimum_ticks=minimum_ticks ? minimum_ticks : 1U;
}
void encoder_edge_filter_irq(encoder_edge_filter_t *s, uint32_t now, uint8_t a, uint8_t b)
{
    a=(uint8_t)!!a; b=(uint8_t)!!b;
    if(s->pending) {
        if(a==s->candidate_a) {
            /* An IRQ without a newly observed A level may be a collapsed
             * edge pair. It must not manufacture a count or prove stability. */
            s->duplicate_irqs++; s->since=now; return;
        }
        if((uint32_t)(now-s->since)>=s->minimum_ticks) accept(s);
        else { s->short_pulses++; s->pending=0; }
    } else if(a==s->stable_a) {
        s->duplicate_irqs++; return;
    }
    candidate(s,now,a,b);
}
void encoder_edge_filter_poll(encoder_edge_filter_t *s, uint32_t now, uint8_t a, uint8_t b)
{
    a=(uint8_t)!!a; b=(uint8_t)!!b;
    if(s->pending) {
        if(a!=s->candidate_a) {
            /* The owner sampled an edge before its ISR. Its exact arrival is
             * unknown: discard the old candidate, never credit an unseen edge. */
            s->short_pulses++; s->pending=0;
        } else if((uint32_t)(now-s->since)>=s->minimum_ticks) accept(s);
    }
    if(!s->pending) candidate(s,now,a,b);
}
