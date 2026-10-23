/* Exercise the production bit-banged reader and its fallback-only RAM probes. */
#include "mock_hardware.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../hardware/Src/ps2.c"

static void read_frame(const uint8_t frame[9], ps2_data *p, unsigned valid)
{
    unsigned i;
    mock_frame(frame, 9);
    assert(ps2_read_buttons(p) == valid);
    mock_assert_poll_complete();
    for (i = 0; i < 9; i++) assert(fallback_ps2_diag.raw[i] == frame[i]);
}
int main(void)
{
    uint8_t f[9] = {0xff, 0x41, 0x5a, 0xff, 0xdf, 0, 0, 0, 0};
    ps2_data p;
    ps2_init();
    read_frame(f, &p, 1);
    assert(p.btn2 == 0x20 && p.LJoy_LR == 128);
    assert(fallback_ps2_diag.circle_presses == 1 && fallback_ps2_diag.circle_releases == 0);
    read_frame(f, &p, 1);
    assert(fallback_ps2_diag.circle_presses == 1);
    memset(f, 0xff, sizeof f);
    read_frame(f, &p, 0);
    assert(!p.mode && !p.btn2);
    f[1] = 0x73; f[2] = 0x5a; f[4] = 0xdf;
    f[5] = 12; f[6] = 34; f[7] = 56; f[8] = 78;
    read_frame(f, &p, 1);
    assert(fallback_ps2_diag.circle_presses == 1); /* No fake release on bad frames. */
    assert(p.RJoy_LR == 12 && p.RJoy_UD == 34 && p.LJoy_LR == 56 && p.LJoy_UD == 78);
    f[4] = 0xff; read_frame(f, &p, 1);
    assert(fallback_ps2_diag.circle_releases == 1);
    f[4] = 0xdf; read_frame(f, &p, 1);
    assert(fallback_ps2_diag.circle_presses == 2);
    f[1] = 0x79; read_frame(f, &p, 0); /* Pressure mode remains explicitly unsupported. */
    assert(fallback_ps2_diag.frames == 7 && fallback_ps2_diag.valid_frames == 5);
    assert(fallback_ps2_diag.last_valid_mode == 0x73 && fallback_ps2_diag.last_valid_buttons == 0x2000);
    puts("PASS: production PS2 polling, circle bit, 0x41/0x73, axes, FF rejection and diagnostic counters");
    return 0;
}
