"""
Pure Python 3 Nintendo SARC archive reader/writer.
Switch target: little-endian (BOM=0xFEFF).

Binary layout:
  SARC header  0x14 bytes
  SFAT header  0x0C bytes
  SFAT nodes   N * 0x10 bytes   (sorted by name hash)
  SFNT header  0x08 bytes
  SFNT data    null-terminated filenames padded to 4-byte alignment
  <padding>    to data_start alignment
  file data    blocks, each aligned per file type
"""
import struct
import os
from typing import Dict, Optional

SARC_MAGIC = b"SARC"
SFAT_MAGIC = b"SFAT"
SFNT_MAGIC = b"SFNT"
HASH_MULT  = 0x65


def _sarc_hash(name: str) -> int:
    h = 0
    for ch in name:
        h = (h * HASH_MULT + ord(ch)) & 0xFFFFFFFF
    return h


def _pad_to(n: int, alignment: int) -> int:
    """Round n up to the next multiple of alignment."""
    if alignment <= 1 or n % alignment == 0:
        return n
    return n + (alignment - n % alignment)


def _file_data_alignment(name: str) -> int:
    ext = os.path.splitext(name)[1].lower()
    return {
        ".bntx": 0x1000,
        ".bnsh": 0x200,
        ".bfres": 0x2000,
        ".szs": 0x2000,
        ".szp": 0x2000,
    }.get(ext, 4)


# ---------------------------------------------------------------------------
# Unpack SARC -> dict
# ---------------------------------------------------------------------------

def unpack(data: bytes) -> Dict[str, bytes]:
    """Parse raw SARC bytes. Returns {filename: bytes}."""
    # SARC header
    magic, hdr_size, bom, total_size, data_off, version, _res = (
        struct.unpack_from("<4sHHIIHH", data, 0)
    )
    if magic != SARC_MAGIC:
        raise ValueError(f"Bad SARC magic: {magic!r}")
    endian = "<" if bom == 0xFEFF else ">"
    off = hdr_size

    # SFAT header
    sfat_magic, sfat_hdr, node_count, hash_mult = struct.unpack_from(
        f"{endian}4sHHI", data, off
    )
    if sfat_magic != SFAT_MAGIC:
        raise ValueError(f"Bad SFAT magic: {sfat_magic!r}")
    off += sfat_hdr

    # SFAT nodes
    nodes = []
    for _ in range(node_count):
        name_hash, name_entry, ds, de = struct.unpack_from(f"{endian}IIII", data, off)
        off += 0x10
        nodes.append((name_hash, name_entry, ds, de))

    # SFNT header
    sfnt_magic, sfnt_hdr, _sfnt_res = struct.unpack_from(f"{endian}4sHH", data, off)
    if sfnt_magic != SFNT_MAGIC:
        raise ValueError(f"Bad SFNT magic: {sfnt_magic!r}")
    sfnt_data_base = off + sfnt_hdr
    off += sfnt_hdr

    # Extract files
    result: Dict[str, bytes] = {}
    for name_hash, name_entry, ds, de in nodes:
        has_name = (name_entry >> 24) & 0xFF
        if has_name:
            name_off = sfnt_data_base + ((name_entry & 0x00FFFFFF) * 4)
            null_pos = data.index(b"\x00", name_off)
            name = data[name_off:null_pos].decode("utf-8")
        else:
            name = f"__nohash_{name_hash:08X}"
        result[name] = bytes(data[data_off + ds : data_off + de])

    return result


# ---------------------------------------------------------------------------
# Pack dict -> SARC
# ---------------------------------------------------------------------------

def pack(files: Dict[str, bytes]) -> bytes:
    """Build a little-endian SARC from {filename: bytes}."""
    # Sort by name hash (Nintendo convention)
    sorted_items = sorted(files.items(), key=lambda kv: _sarc_hash(kv[0]))

    # Build filename table
    fname_table = bytearray()
    fname_offset_units: Dict[str, int] = {}
    for name, _ in sorted_items:
        # Offset in units of 4 bytes from SFNT data start
        unit = len(fname_table) // 4
        fname_offset_units[name] = unit
        encoded = name.encode("utf-8") + b"\x00"
        fname_table += encoded
        # Pad to 4-byte boundary
        rem = len(fname_table) % 4
        if rem:
            fname_table += b"\x00" * (4 - rem)

    # Header layout
    sarc_hdr_size  = 0x14
    sfat_hdr_size  = 0x0C
    sfat_node_size = 0x10
    sfnt_hdr_size  = 0x08
    node_count     = len(sorted_items)

    headers_size = (sarc_hdr_size + sfat_hdr_size
                    + node_count * sfat_node_size
                    + sfnt_hdr_size + len(fname_table))

    # Data section start (must be at least 4-byte aligned; often 0x100)
    data_start = _pad_to(headers_size, 0x100)

    # Compute per-file offsets within data section
    data_cursor = 0
    file_ranges = []  # (data_start_rel, data_end_rel)
    for name, file_bytes in sorted_items:
        align = _file_data_alignment(name)
        data_cursor = _pad_to(data_cursor, align)
        start = data_cursor
        end   = start + len(file_bytes)
        file_ranges.append((start, end))
        data_cursor = end

    total_size = data_start + data_cursor

    # Assemble binary
    buf = bytearray()

    # SARC header (little-endian)
    buf += struct.pack("<4sHHIIHH",
                       SARC_MAGIC,
                       sarc_hdr_size,
                       0xFEFF,         # BOM: little-endian
                       total_size,
                       data_start,
                       0x0100,         # version
                       0x0000)         # reserved

    # SFAT header
    buf += struct.pack("<4sHHI",
                       SFAT_MAGIC,
                       sfat_hdr_size,
                       node_count,
                       HASH_MULT)

    # SFAT nodes
    for (name, _), (ds, de) in zip(sorted_items, file_ranges):
        name_hash  = _sarc_hash(name)
        name_entry = (0x01 << 24) | fname_offset_units[name]
        buf += struct.pack("<IIII", name_hash, name_entry, ds, de)

    # SFNT header
    buf += struct.pack("<4sHH",
                       SFNT_MAGIC,
                       sfnt_hdr_size,
                       0x0000)

    # Filename table
    buf += fname_table

    # Pad to data_start
    pad = data_start - len(buf)
    if pad < 0:
        raise RuntimeError(f"Header overflowed data_start by {-pad} bytes")
    buf += b"\x00" * pad

    # File data
    for (name, file_bytes), (ds, de) in zip(sorted_items, file_ranges):
        current_rel = len(buf) - data_start
        if current_rel < ds:
            buf += b"\x00" * (ds - current_rel)
        buf += file_bytes

    return bytes(buf)


# ---------------------------------------------------------------------------
# SarcArchive class (thin wrapper)
# ---------------------------------------------------------------------------

class SarcArchive:
    """Convenience wrapper around pack/unpack."""

    def __init__(self, files: Optional[Dict[str, bytes]] = None):
        self.files: Dict[str, bytes] = {} if files is None else dict(files)

    @classmethod
    def from_bytes(cls, data: bytes) -> "SarcArchive":
        return cls(unpack(data))

    def to_bytes(self) -> bytes:
        return pack(self.files)

    def __getitem__(self, name: str) -> bytes:
        return self.files[name]

    def __setitem__(self, name: str, data: bytes) -> None:
        self.files = {**self.files, name: data}

    def __contains__(self, name: str) -> bool:
        return name in self.files

    def list(self):
        return sorted(self.files.keys())
