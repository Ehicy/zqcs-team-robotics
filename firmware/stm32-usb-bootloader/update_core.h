#ifndef CRTC_UPDATE_CORE_H
#define CRTC_UPDATE_CORE_H
#include <stdint.h>
#include <stdbool.h>

#define UPDATE_BASE UINT32_C(0x08002000)
#define UPDATE_END UINT32_C(0x0800F800)
#define UPDATE_PAGE 1024u
#define UPDATE_CAPACITY (UPDATE_END - UPDATE_BASE)

/* CRTCDFU1 envelope: one 1024-byte header, then APP bytes (not a DfuSe file).
 * LE u32 fields at 8/12/16/20/24: version/base/length/bodyCRC/headerCRC.
 * headerCRC covers bytes 0..23. Body CRC is IEEE CRC32 (zlib compatible).
 * Sequential 1 KiB blocks, only the final body block may be short. */
typedef struct {
    bool (*erase)(uint32_t address);
    bool (*write)(uint32_t address, uint32_t value);
    uint32_t (*read)(uint32_t address);
    void (*feed)(void);
} update_io_t;
typedef struct {
    uint32_t length, expected_crc, received, crc, vectors[2];
    uint16_t next_block;
    bool active, failed, complete;
} update_t;

uint32_t update_crc(uint32_t crc, const uint8_t *data, uint32_t size);
bool update_vectors_valid(uint32_t sp, uint32_t pc, uint32_t length);
void update_init(update_t *s);
bool update_accept(update_t *s, const update_io_t *io, uint16_t block,
                   const uint8_t *data, uint16_t size);
bool update_finish(update_t *s, const update_io_t *io, uint16_t block);
#endif
