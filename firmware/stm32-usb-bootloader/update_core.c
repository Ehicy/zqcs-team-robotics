#include "update_core.h"

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
uint32_t update_crc(uint32_t crc, const uint8_t *data, uint32_t size) {
    while (size--) {
        crc ^= *data++;
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & (0u - (crc & 1u)));
    }
    return crc;
}
bool update_vectors_valid(uint32_t sp, uint32_t pc, uint32_t length) {
    return length >= 256 && length <= UPDATE_CAPACITY &&
           sp > UINT32_C(0x20000000) && sp <= UINT32_C(0x20005000) &&
           (sp & 7u) == 0 && (pc & 1u) != 0 &&
           (pc & ~1u) >= UPDATE_BASE + 256 && (pc & ~1u) < UPDATE_BASE + length;
}
void update_init(update_t *s) {
    *s = (update_t){0};
}
static bool fail(update_t *s) { s->failed = true; s->active = false; return false; }

bool update_accept(update_t *s, const update_io_t *io, uint16_t block,
                   const uint8_t *data, uint16_t size) {
    if (s->failed || s->complete || block != s->next_block || !data ||
        size == 0 || size > UPDATE_PAGE) return fail(s);
    if (block == 0) {
        static const uint8_t magic[] = {'C','R','T','C','D','F','U','1'};
        if (size != UPDATE_PAGE) return fail(s);
        for (unsigned i = 0; i < sizeof magic; ++i)
            if (data[i] != magic[i]) return fail(s);
        s->length = le32(data + 16);
        s->expected_crc = le32(data + 20);
        if (le32(data + 8) != 1 || le32(data + 12) != UPDATE_BASE ||
            s->length < 256 || s->length > UPDATE_CAPACITY || (s->length & 3u) ||
            (update_crc(UINT32_MAX, data, 24) ^ UINT32_MAX) != le32(data + 24))
            return fail(s);
        /* Invalidate old vectors before changing any other application page. */
        if (!io->erase(UPDATE_BASE)) return fail(s);
        s->crc = UINT32_MAX;
        s->active = true;
    } else {
        if (!s->active || s->received >= s->length) return fail(s);
        uint32_t remaining = s->length - s->received;
        uint32_t expected = remaining < UPDATE_PAGE ? remaining : UPDATE_PAGE;
        if (size != expected || s->received % UPDATE_PAGE) return fail(s);
        uint32_t address = UPDATE_BASE + s->received;
        unsigned start = 0;
        if (s->received == 0) {
            s->vectors[0] = le32(data); s->vectors[1] = le32(data + 4);
            if (!update_vectors_valid(s->vectors[0], s->vectors[1], s->length))
                return fail(s);
            start = 8; /* MSP/reset vectors are committed only after full CRC. */
        } else if (!io->erase(address)) return fail(s);
        for (unsigned i = start; i < size; i += 4) {
            io->feed();
            if (!io->write(address + i, le32(data + i))) return fail(s);
        }
        s->crc = update_crc(s->crc, data, size);
        s->received += size;
    }
    io->feed();
    ++s->next_block;
    return true;
}
bool update_finish(update_t *s, const update_io_t *io, uint16_t block) {
    if (!s->active || s->failed || s->complete || block != s->next_block ||
        s->received != s->length || (s->crc ^ UINT32_MAX) != s->expected_crc)
        return fail(s);
    uint32_t crc = UINT32_MAX;
    for (uint32_t off = 0; off < s->length; off += 4) {
        uint32_t w = off < 8 ? s->vectors[off / 4] : io->read(UPDATE_BASE + off);
        uint8_t bytes[4] = {(uint8_t)w, (uint8_t)(w >> 8), (uint8_t)(w >> 16), (uint8_t)(w >> 24)};
        crc = update_crc(crc, bytes, 4);
        io->feed();
    }
    if ((crc ^ UINT32_MAX) != s->expected_crc) return fail(s);
    /* Flash write primitive writes the high half first. An uncommitted MSP is
     * 0xFFFFFFFF or 0x2000FFFF, both rejected by the boot vector validator. */
    if (!io->write(UPDATE_BASE + 4, s->vectors[1]) ||
        !io->write(UPDATE_BASE, s->vectors[0])) return fail(s);
    s->active = false;
    s->complete = true;
    return true;
}
