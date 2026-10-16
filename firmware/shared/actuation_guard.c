#include "actuation_guard.h"
void actuation_guard_record(actuation_guard_t *g,uint8_t mode,uint32_t now)
{ (void)now; if(mode) { g->last_mode=mode; g->active=1; } }
void actuation_guard_stop(actuation_guard_t *g,uint32_t now)
{ if(g->active) { g->active=0; g->stopped_ms=now; } }
int actuation_guard_idle(const actuation_guard_t *g,uint32_t now)
{ return !g->last_mode || (!g->active && (uint32_t)(now-g->stopped_ms)>=ACTUATION_MODE_HOLD_MS); }
int actuation_guard_allows(const actuation_guard_t *g,uint8_t mode,uint32_t now)
{ return mode==g->last_mode || actuation_guard_idle(g,now); }
