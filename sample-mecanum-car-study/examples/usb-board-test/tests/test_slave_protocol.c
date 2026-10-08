#include "slave_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint32_t sequence = 0;
    const char *command = NULL;
    assert(slave_parse_request("REQ 00000001 PING", &sequence, &command));
    assert(sequence == 1 && !strcmp(command, "PING"));
    assert(slave_parse_request("REQ fFfFFFFF LED TOGGLE", &sequence, &command));
    assert(sequence == UINT32_MAX && !strcmp(command, "LED TOGGLE"));
    const char *invalid[] = {"", "REQ", "REQ ", "REQ 0000000", "REQ 00000001",
        "REQ 00000001 ", "REQ 00000000 LED ON", "REQ 0000000G PING",
        "REQ 000000001 PING", "REQ 00000001PING", "RSP 00000001 PONG",
        "PING", "UART REQ 00000001 PING"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        sequence = 0x12345678;
        command = "untouched";
        assert(!slave_parse_request(invalid[i], &sequence, &command));
        assert(sequence == 0x12345678 && !strcmp(command, "untouched"));
    }
    /* Truncated header at each byte boundary must never reach a command. */
    char partial[] = "REQ 00000001 PING";
    for (unsigned i = 0; i < 14; ++i) {
        char saved = partial[i];
        partial[i] = 0;
        assert(!slave_parse_request(partial, &sequence, &command));
        partial[i] = saved;
    }
    puts("PASS: valid requests, IDs, truncated headers, ignored responses");
    return 0;
}
