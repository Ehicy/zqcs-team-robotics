#include "slave_protocol.h"
#include <string.h>

bool slave_parse_request(const char *line, uint32_t *sequence, const char **command)
{
    if (strlen(line) < 14 || strncmp(line, "REQ ", 4) || line[12] != ' ')
        return false;
    uint32_t value = 0;
    for (unsigned i = 4; i < 12; ++i) {
        unsigned digit;
        char c = line[i];
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A') + 10U;
        else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a') + 10U;
        else return false;
        value = (value << 4) | digit;
    }
    if (!value) return false;
    *sequence = value;
    *command = line + 13;
    return true;
}
