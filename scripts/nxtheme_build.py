#!/usr/bin/env python3
"""
nxtheme_build.py  --  Build / Extract / Roundtrip .nxtheme files.

Usage:
  python3 nxtheme_build.py <input_dir> <output.nxtheme>
  python3 nxtheme_build.py --extract <input.nxtheme> <output_dir>
  python3 nxtheme_build.py --roundtrip <input.nxtheme>

Input directory for build mode:
  info.json    REQUIRED  e.g. {"ThemeName":"QOS","Author":"Astral","LayoutInfo":""}
  layout.json  optional
  image.jpg    optional  (1280x720 JPEG)
  image.dds    optional  (DXT1 DDS)
  common.json  optional
"""
import sys
import os
import json
import hashlib

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _SCRIPT_DIR)

from nxtheme_lib import NxTheme
from nxtheme_lib.yaz0 import decompress as yaz0_dec, compress as yaz0_enc
from nxtheme_lib.sarc import unpack as sarc_unpack, pack as sarc_pack


def cmd_build(input_dir: str, output_path: str) -> None:
    if not os.path.isdir(input_dir):
        sys.exit(f"ERROR: input directory not found: {input_dir}")

    files = {}
    for fname in sorted(os.listdir(input_dir)):
        fpath = os.path.join(input_dir, fname)
        if os.path.isfile(fpath):
            with open(fpath, "rb") as fh:
                files[fname] = fh.read()

    if not files:
        sys.exit("ERROR: input directory is empty")

    theme = NxTheme(files)
    try:
        theme.validate()
    except ValueError as exc:
        sys.exit(f"ERROR: {exc}")

    theme.to_file(output_path, compress=True)
    size_kb = os.path.getsize(output_path) / 1024
    print(f"Built {output_path}  ({size_kb:.1f} KB)")
    for name in theme.list_files():
        print(f"  {len(files[name]):8d}  {name}")


def cmd_extract(input_path: str, output_dir: str) -> None:
    if not os.path.exists(input_path):
        sys.exit(f"ERROR: file not found: {input_path}")

    theme = NxTheme.from_file(input_path)
    os.makedirs(output_dir, exist_ok=True)
    for name in theme.list_files():
        data = theme.get_file(name)
        out_path = os.path.join(output_dir, name)
        parent = os.path.dirname(os.path.abspath(out_path))
        if parent:
            os.makedirs(parent, exist_ok=True)
        with open(out_path, "wb") as fh:
            fh.write(data)
        print(f"  {len(data):8d}  {name}")
    print(f"Extracted {len(theme.list_files())} files to {output_dir}")


def cmd_roundtrip(input_path: str) -> None:
    if not os.path.exists(input_path):
        sys.exit(f"ERROR: file not found: {input_path}")

    print(f"Round-trip: {os.path.basename(input_path)}")
    with open(input_path, "rb") as fh:
        raw = fh.read()

    # Decompress Yaz0
    sarc_bytes = yaz0_dec(raw)
    print(f"  Yaz0 decompress:  {len(raw):,} -> {len(sarc_bytes):,} bytes  OK")

    # Unpack SARC
    files = sarc_unpack(sarc_bytes)
    print(f"  SARC unpack:      {len(files)} files")
    for name, data in sorted(files.items()):
        print(f"    {len(data):8d}  {name}")

    # Repack SARC
    repacked_sarc = sarc_pack(files)
    print(f"  SARC repack:      {len(repacked_sarc):,} bytes  OK")

    # Re-compress Yaz0
    repacked_yaz0 = yaz0_enc(repacked_sarc)
    print(f"  Yaz0 recompress:  {len(repacked_yaz0):,} bytes  OK")

    # Verify file content hashes through full round-trip
    check_files = sarc_unpack(yaz0_dec(repacked_yaz0))
    mismatches = []
    print(f"  File content verification:")
    for name in sorted(files):
        orig_hash = hashlib.sha256(files[name]).hexdigest()[:16]
        new_hash  = hashlib.sha256(check_files.get(name, b"")).hexdigest()[:16]
        status = "OK" if orig_hash == new_hash else "MISMATCH"
        print(f"    [{status}] {name}  orig={orig_hash} new={new_hash}")
        if status != "OK":
            mismatches.append(name)

    if mismatches:
        print(f"\nROUND-TRIP FAILED: {len(mismatches)} file(s) differ: {mismatches}")
        sys.exit(1)
    print(f"\nROUND-TRIP PASS: all {len(files)} files content-verified")


def main() -> None:
    args = sys.argv[1:]
    if len(args) == 2 and not args[0].startswith("--"):
        cmd_build(args[0], args[1])
    elif len(args) == 3 and args[0] == "--extract":
        cmd_extract(args[1], args[2])
    elif len(args) == 2 and args[0] == "--roundtrip":
        cmd_roundtrip(args[1])
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
