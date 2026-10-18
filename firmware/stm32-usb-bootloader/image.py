"""CRTCDFU1 package codec. No USB or hardware access."""
import struct
import zlib

BASE, END, PAGE = 0x08002000, 0x0800F800, 1024
MAGIC = b"CRTCDFU1"

def validate_app(app):
    if len(app) < 256 or len(app) > END - BASE or len(app) % 4:
        raise ValueError("APP must be 4-byte aligned and fit 0x08002000..0x0800F800")
    sp, pc = struct.unpack_from('<II', app)
    if not (0x20000000 < sp <= 0x20005000 and sp % 8 == 0 and pc & 1 and
            BASE + 256 <= (pc & ~1) < BASE + len(app)):
        raise ValueError("Invalid relocated APP vectors (do not use the direct ST-Link BIN)")

def package(app):
    validate_app(app)
    header = MAGIC + struct.pack('<IIII', 1, BASE, len(app), zlib.crc32(app))
    header += struct.pack('<I', zlib.crc32(header))
    return header.ljust(PAGE, b'\xff') + app

def unpack(blob):
    if len(blob) < PAGE or blob[:8] != MAGIC:
        raise ValueError("Expected CRTCDFU1 package, not raw BIN or ST DfuSe")
    version, base, size, crc, header_crc = struct.unpack_from('<IIIII', blob, 8)
    app = blob[PAGE:]
    if (version != 1 or base != BASE or size != len(app) or
            crc != zlib.crc32(app) or header_crc != zlib.crc32(blob[:24])):
        raise ValueError("Package length/address/version/CRC mismatch")
    validate_app(app)
    return app
