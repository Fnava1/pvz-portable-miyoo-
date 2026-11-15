#!/usr/bin/env python3
"""
Offline tool to inspect, validate, and manage pre-baked sprite cache files (.spc).
Format:
  - Header: 32 bytes (Magic: 'SPC1', Version: 1, Format: ARGB4444, Dimensions, Offsets, FrameCount: 24)
  - Data: FrameSizeBytes * FrameCount raw 16-bit pixels.
"""

import os
import sys
import struct
from pathlib import Path

SPC_MAGIC = 0x53504331
SPC_VERSION = 2
SPC_FORMAT_ARGB4444 = 1
HEADER_FORMAT = "<IHHHHhhHII6s" # 32 bytes total
# Fields:
# Magic (4B), Version (2B), Format (2B), Width (2B), Height (2B),
# OffsetX (2B), OffsetY (2B), FrameCount (2B), FrameSizeBytes (4B), TotalDataBytes (4B), Reserved (6B)

def inspect_spc(file_path: Path):
    if not file_path.is_file():
        print(f"Error: {file_path} is not a file.")
        return False

    data = file_path.read_bytes()
    if len(data) < 32:
        print(f"Error: {file_path} is too small (< 32 bytes).")
        return False

    magic, ver, fmt, w, h, ox, oy, frames, frame_bytes, total_bytes, _ = struct.unpack_from("<IHHHHhhHII6s", data, 0)
    print(f"File: {file_path.name}")
    print(f"  Magic:           0x{magic:08X} ({'VALID' if magic == SPC_MAGIC else 'INVALID'})")
    print(f"  Version:         {ver} ({'VALID' if ver == SPC_VERSION else 'MISMATCH'})")
    print(f"  Format:          {fmt} (ARGB4444)")
    print(f"  Resolution:      {w}x{h}")
    print(f"  Offset:          ({ox}, {oy})")
    print(f"  Frames:          {frames}")
    print(f"  Frame Size:      {frame_bytes} bytes")
    print(f"  Total Data:      {total_bytes} bytes ({total_bytes / (1024*1024):.2f} MB)")
    expected_data_len = frame_bytes * frames
    actual_data_len = len(data) - 32
    if actual_data_len != expected_data_len:
        print(f"  WARNING: Payload mismatch! Expected {expected_data_len}, got {actual_data_len}")
        return False
    else:
        print("  Integrity:       OK")
        return True

def audit_directory(cache_dir: Path):
    if not cache_dir.exists():
        print(f"Directory {cache_dir} does not exist.")
        return 1

    version_file = cache_dir / ".version"
    if version_file.exists():
        print(f"Found .version: {version_file.read_text().strip()}")
    else:
        print("WARNING: .version file missing!")

    spc_files = sorted(list(cache_dir.glob("*.spc")))
    print(f"Found {len(spc_files)} .spc files in {cache_dir}:")
    total_ram = 0
    valid_count = 0
    for f in spc_files:
        data = f.read_bytes()
        if len(data) >= 32:
            magic, ver, fmt, w, h, ox, oy, frames, frame_bytes, total_bytes, _ = struct.unpack_from("<IHHHHhhHII6s", data, 0)
            if magic == SPC_MAGIC:
                total_ram += total_bytes
                valid_count += 1
                print(f"  [OK] {f.name:<30} {w}x{h:<8} offset=({ox:>3},{oy:>3}) {total_bytes / 1024:>6.1f} KB")
            else:
                print(f"  [BAD MAGIC] {f.name}")
        else:
            print(f"  [CORRUPT] {f.name}")

    print("-" * 60)
    print(f"Summary: {valid_count}/{len(spc_files)} valid files.")
    print(f"Total uncompressed RAM footprint: {total_ram / (1024*1024):.2f} MB (Budget: 15.0 MB)")
    return 0

def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python tools/bake_spritesheets.py inspect <path_to_file.spc>")
        print("  python tools/bake_spritesheets.py audit [cache_dir]")
        sys.exit(1)

    cmd = sys.argv[1]
    if cmd == "inspect":
        if len(sys.argv) < 3:
            print("Specify file to inspect.")
            sys.exit(1)
        inspect_spc(Path(sys.argv[2]))
    elif cmd == "audit":
        dir_path = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("cached_sprites")
        sys.exit(audit_directory(dir_path))
    else:
        print(f"Unknown command: {cmd}")
        sys.exit(1)

if __name__ == "__main__":
    main()
