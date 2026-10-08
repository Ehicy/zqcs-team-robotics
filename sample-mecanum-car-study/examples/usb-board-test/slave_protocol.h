#ifndef SLAVE_TEST_PROTOCOL_H
#define SLAVE_TEST_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>

/* REQ <eight hexadecimal digits> <command>; responses never parse as requests. */
bool slave_parse_request(const char *line, uint32_t *sequence, const char **command);
#endif
