"""
Pure Python 3 Yaz0 encoder/decoder for Nintendo Switch nxtheme files.
No C extensions. No third-party deps. stdlib struct only.

Header (16 bytes):
  b"Yaz0"  (4)   - magic
  uint32 BE (4)  - decompressed size
  reserved  (8)  - zeros

Data: groups of 8 ops, each group starts with a code byte.
  code bit = 1 (MSB first): copy 1 literal byte from input
  code bit = 0: back-reference
    byte1, byte2 = next 2 bytes
    dist   = ((byte1 & 0x0F) << 8) | byte2     distance-1 from cur
    nibble = byte1 >> 4
    if nibble != 0: count = nibble + 2
    else:           count = byte3 + 0x12        (3rd byte follows)
"""
import struct
from io import BytesIO


# ---------------------------------------------------------------------------
# Decompress
# ---------------------------------------------------------------------------

def decompress(data: bytes) -> bytes:
    """Decompress Yaz0 bytes. Raises ValueError on bad magic."""
    if len(data) < 16:
        raise ValueError("Data too short to be Yaz0")
    if data[:4] != b"Yaz0":
        raise ValueError(f"Not Yaz0 data (magic={data[:4]!r})")
    dec_size: int = struct.unpack_from(">I", data, 4)[0]
    src = memoryview(data)[16:]   # skip header
    src_pos = 0
    out = bytearray(dec_size)
    out_pos = 0

    while out_pos < dec_size:
        if src_pos >= len(src):
            break
        code = src[src_pos]
        src_pos += 1

        for bit in range(7, -1, -1):   # MSB first
            if out_pos >= dec_size:
                break
            if (code >> bit) & 1:
                # Literal
                if src_pos >= len(src):
                    break
                out[out_pos] = src[src_pos]
                src_pos += 1
                out_pos += 1
            else:
                # Back-reference
                if src_pos + 1 >= len(src):
                    break
                b1 = src[src_pos]
                b2 = src[src_pos + 1]
                src_pos += 2
                dist   = ((b1 & 0x0F) << 8) | b2
                nibble = b1 >> 4
                if nibble != 0:
                    count = nibble + 2
                else:
                    if src_pos >= len(src):
                        break
                    count = src[src_pos] + 0x12
                    src_pos += 1
                copy_from = out_pos - dist - 1
                for _ in range(count):
                    if out_pos >= dec_size:
                        break
                    out[out_pos] = out[copy_from]
                    out_pos += 1
                    copy_from += 1

    return bytes(out)


# ---------------------------------------------------------------------------
# Compress  (greedy LZ77, window=4096, match 3-18/273 bytes)
# ---------------------------------------------------------------------------

def compress(data: bytes) -> bytes:
    """Compress bytes to Yaz0. Returns complete Yaz0-encoded bytes."""
    src_len = len(data)
    out = BytesIO()
    # Write header
    out.write(b"Yaz0")
    out.write(struct.pack(">I", src_len))
    out.write(b"\x00" * 8)

    pos = 0
    while pos < src_len:
        flags     = 0
        group_out = BytesIO()

        for bit in range(7, -1, -1):
            if pos >= src_len:
                break

            # Search window [max(0, pos-4096), pos) for longest match
            win_start = max(0, pos - 4096)
            max_match = min(273, src_len - pos)   # 273 = 0x12 + 0xFF
            best_len  = 2
            best_dist = 0

            for wpos in range(pos - 1, win_start - 1, -1):
                match_len = 0
                while match_len < max_match:
                    if data[wpos + match_len] != data[pos + match_len]:
                        break
                    match_len += 1
                if match_len > best_len:
                    best_len  = match_len
                    best_dist = pos - wpos - 1
                    if best_len >= max_match:
                        break

            if best_len >= 3:
                # Encode back-reference
                dist  = best_dist
                count = best_len
                if count <= 17:          # count-2 fits in nibble (2..17)
                    b1 = ((count - 2) << 4) | ((dist >> 8) & 0x0F)
                    b2 = dist & 0xFF
                    group_out.write(bytes([b1, b2]))
                else:                   # count 18..273, nibble=0
                    b1 = (dist >> 8) & 0x0F
                    b2 = dist & 0xFF
                    b3 = count - 0x12
                    group_out.write(bytes([b1, b2, b3]))
                pos += count
                # bit stays 0 (back-reference)
            else:
                # Literal
                group_out.write(bytes([data[pos]]))
                flags |= (1 << bit)
                pos   += 1

        out.write(bytes([flags]))
        out.write(group_out.getvalue())

    return out.getvalue()
