#!/usr/bin/env python3
"""
PopCap Games PAK Container Codec (PakLib)
=========================================
Implements reading, extraction, random-access linear seeking, and packing
for PopCap main.pak / main_mmp.pak archives.

Container binary specification:
- Header:
    uint32 magic   (0xBAC04AC0, little-endian)
    uint32 version (0x00000000, little-endian)
- Records table:
    uint8  flags       (0x00; if flag & 0x80 -> table ends)
    uint8  name_len    (length of relative filename)
    char   name[len]   (Latin-1 relative path using '\\')
    int32  size        (file size in bytes)
    uint32 low_time    (FILETIME dwLowDateTime)
    uint32 high_time   (FILETIME dwHighDateTime)
    ... terminates with a record where (flags & 0x80) != 0 (typically 0x80 byte)
- Data payload:
    Concatenated byte streams in exact order of the records table.
    Each byte is XORed with 0xF7.
"""

import os
import sys
import struct
from typing import List, Dict, Tuple, Optional, Union

POP_PAK_MAGIC = 0xBAC04AC0
POP_PAK_VERSION = 0
FILEFLAGS_END = 0x80
XOR_KEY = 0xF7


class PakError(Exception):
    """Custom exception raised for invalid or corrupted PAK container data."""
    pass


class PakRecord:
    """Represents a single file entry metadata record inside a PAK container."""

    def __init__(
        self,
        name: str,
        size: int,
        offset: int = 0,
        flags: int = 0,
        filetime: Tuple[int, int] = (0, 0),
    ):
        self.name = name
        self.size = size
        self.offset = offset
        self.flags = flags
        self.filetime = filetime

    def __repr__(self) -> str:
        return f"<PakRecord name={self.name!r} size={self.size} offset={self.offset}>"


class PakArchive:
    """PopCap PAK archive reader, extractor, and writer."""

    def __init__(self, pak_path: Optional[str] = None):
        self.pak_path: Optional[str] = pak_path
        self.records: Dict[str, PakRecord] = {}
        self.record_list: List[PakRecord] = []
        self._fp = None
        if pak_path and os.path.isfile(pak_path):
            self.open(pak_path)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def close(self):
        """Close archive handle if open."""
        if self._fp is not None:
            try:
                self._fp.close()
            except Exception:
                pass
            self._fp = None

    def open(self, pak_path: str):
        """Open and parse an existing PAK archive header and record table."""
        self.close()
        self.pak_path = os.path.abspath(pak_path)
        self.records.clear()
        self.record_list.clear()

        if not os.path.isfile(self.pak_path):
            raise FileNotFoundError(f"PAK archive not found: {self.pak_path}")

        self._fp = open(self.pak_path, "rb")
        try:
            # 1. Read header magic and version
            header = self._fp.read(8)
            if len(header) < 8:
                raise PakError(f"Unexpected EOF reading PAK header from {self.pak_path}")

            magic, version = struct.unpack("<II", header)
            if magic != POP_PAK_MAGIC:
                raise PakError(
                    f"Invalid PAK magic 0x{magic:08X} in {self.pak_path} "
                    f"(expected 0x{POP_PAK_MAGIC:08X})"
                )
            if version != POP_PAK_VERSION:
                raise PakError(
                    f"Unsupported PAK version {version} in {self.pak_path} "
                    f"(expected {POP_PAK_VERSION})"
                )

            # 2. Parse file records table
            parsed_entries = []
            while True:
                flag_bytes = self._fp.read(1)
                if not flag_bytes:
                    break
                flags = flag_bytes[0]
                if flags & FILEFLAGS_END:
                    break

                name_len_byte = self._fp.read(1)
                if not name_len_byte:
                    raise PakError("Unexpected EOF reading record name length")
                name_len = name_len_byte[0]

                name_bytes = self._fp.read(name_len)
                if len(name_bytes) < name_len:
                    raise PakError("Unexpected EOF reading record filename")
                rel_name = name_bytes.decode("latin-1")

                meta = self._fp.read(12)
                if len(meta) < 12:
                    raise PakError("Unexpected EOF reading record metadata")
                size, low_time, high_time = struct.unpack("<iII", meta)
                parsed_entries.append((flags, rel_name, size, (low_time, high_time)))

            # 3. Calculate absolute file offsets in payload block
            current_offset = self._fp.tell()
            for flags, rel_name, size, filetime in parsed_entries:
                record = PakRecord(
                    name=rel_name,
                    size=size,
                    offset=current_offset,
                    flags=flags,
                    filetime=filetime,
                )
                self.record_list.append(record)
                # Normalize lookup key (uppercase with backslashes)
                norm_key = rel_name.replace("/", "\\").upper()
                self.records[norm_key] = record
                current_offset += size
        except Exception:
            self.close()
            raise

    def list_files(self) -> List[str]:
        """Return list of all relative file paths inside the archive."""
        return [r.name for r in self.record_list]

    def get_record(self, name: str) -> Optional[PakRecord]:
        """Lookup a file record by name (case and slash insensitive)."""
        norm_key = name.replace("/", "\\").upper()
        return self.records.get(norm_key)

    def read_file(self, name: str) -> bytes:
        """
        Read and decrypt a single file's contents from the archive via linear seek.
        Does not decompress or load the entire archive into memory.
        """
        if self._fp is None:
            raise PakError("Archive is not open.")

        record = self.get_record(name)
        if record is None:
            raise FileNotFoundError(f"File '{name}' not found in PAK archive.")

        self._fp.seek(record.offset)
        raw_data = self._fp.read(record.size)
        if len(raw_data) < record.size:
            raise PakError(
                f"Truncated data for '{name}' (expected {record.size} bytes, got {len(raw_data)})"
            )

        # Fast XOR 0xF7 decryption
        return bytes(b ^ XOR_KEY for b in raw_data)

    def unpack_file(self, name: str, dest_path: Optional[str] = None) -> bytes:
        """
        Extract and decrypt a single file.
        If dest_path is specified, writes the file to disk creating parent directories.
        Returns the decrypted bytes.
        """
        data = self.read_file(name)
        if dest_path:
            os.makedirs(os.path.dirname(os.path.abspath(dest_path)), exist_ok=True)
            with open(dest_path, "wb") as f:
                f.write(data)
        return data

    def extract_all(self, dest_dir: str, log_fn=None):
        """Extract all files in the archive to dest_dir."""
        dest_dir = os.path.abspath(dest_dir)
        os.makedirs(dest_dir, exist_ok=True)

        for record in self.record_list:
            disk_rel_path = record.name.replace("\\", os.sep).replace("/", os.sep)
            dest_file = os.path.join(dest_dir, disk_rel_path)
            self.unpack_file(record.name, dest_file)
            if log_fn:
                log_fn(f"Extracted: {record.name}")

    @classmethod
    def create_from_entries(
        cls,
        entries: List[Tuple[str, Union[bytes, str]]],
        out_pak_path: str,
        log_fn=None,
    ):
        """
        Create a new PopCap PAK archive from a list of (rel_path, data_or_source_path).
        """
        os.makedirs(os.path.dirname(os.path.abspath(out_pak_path)), exist_ok=True)

        prepared_entries = []
        for item in entries:
            rel_path, source = item
            # Standardize backslashes for PopCap engine compatibility
            norm_path = rel_path.replace("/", "\\").lstrip("\\")
            name_bytes = norm_path.encode("latin-1")
            if len(name_bytes) > 255:
                raise ValueError(f"Path too long ({len(name_bytes)} bytes): {norm_path}")

            if isinstance(source, bytes):
                size = len(source)
                data_or_path = source
            elif isinstance(source, str):
                size = os.path.getsize(source)
                data_or_path = source
            else:
                raise TypeError(f"Invalid entry source type for {rel_path}: {type(source)}")

            prepared_entries.append((norm_path, name_bytes, size, data_or_path))

        with open(out_pak_path, "wb") as out_fp:
            # 1. Header: Magic and Version
            out_fp.write(struct.pack("<II", POP_PAK_MAGIC, POP_PAK_VERSION))

            # 2. File records table
            for norm_path, name_bytes, size, _ in prepared_entries:
                out_fp.write(struct.pack("<BB", 0x00, len(name_bytes)))
                out_fp.write(name_bytes)
                out_fp.write(struct.pack("<i", size))
                out_fp.write(struct.pack("<II", 0, 0))  # FILETIME

            # 3. Terminate records table with 0x80
            out_fp.write(struct.pack("<B", FILEFLAGS_END))

            # 4. Stream data payload with XOR 0xF7 encryption
            for norm_path, _, size, data_or_path in prepared_entries:
                if isinstance(data_or_path, bytes):
                    payload = bytes(b ^ XOR_KEY for b in data_or_path)
                    out_fp.write(payload)
                else:
                    with open(data_or_path, "rb") as in_fp:
                        chunk_size = 64 * 1024
                        while True:
                            chunk = in_fp.read(chunk_size)
                            if not chunk:
                                break
                            out_fp.write(bytes(b ^ XOR_KEY for b in chunk))

        if log_fn:
            log_fn(f"Created PAK archive: {out_pak_path} ({len(prepared_entries)} files)")

    @classmethod
    def create_from_dir(
        cls,
        src_dir: str,
        out_pak_path: str,
        target_dirs: Optional[List[str]] = None,
        log_fn=None,
    ):
        """
        Scan src_dir and pack matching files into out_pak_path.
        If target_dirs is specified, only those subdirectories are scanned.
        Otherwise all files under src_dir are packed.
        """
        src_dir = os.path.abspath(src_dir)
        entries: List[Tuple[str, str]] = []

        if target_dirs:
            for d in target_dirs:
                d_path = os.path.join(src_dir, d)
                if not os.path.isdir(d_path):
                    continue
                for root, _, files in os.walk(d_path):
                    for f in sorted(files):
                        full = os.path.join(root, f)
                        rel = os.path.relpath(full, src_dir)
                        entries.append((rel, full))
        else:
            for root, _, files in os.walk(src_dir):
                for f in sorted(files):
                    full = os.path.join(root, f)
                    rel = os.path.relpath(full, src_dir)
                    entries.append((rel, full))

        # Deterministic sorting
        entries.sort(key=lambda x: x[0].lower())
        cls.create_from_entries(entries, out_pak_path, log_fn=log_fn)


def pack_pak(input_dir: str, output_pak_path: str, target_dirs: Optional[List[str]] = None):
    """Convenience function: pack directory into a PopCap PAK container."""
    PakArchive.create_from_dir(input_dir, output_pak_path, target_dirs=target_dirs)


def unpack_pak(pak_path: str, output_dir: str) -> bool:
    """Convenience function: unpack PopCap PAK container into output directory."""
    with PakArchive(pak_path) as archive:
        archive.extract_all(output_dir)
    return True


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage:")
        print("  python pak_lib.py unpack <archive.pak> <output_dir>")
        print("  python pak_lib.py pack <source_dir> <output_archive.pak>")
        sys.exit(1)

    cmd = sys.argv[1].lower()
    if cmd == "unpack":
        pak_file = sys.argv[2]
        out_dir = sys.argv[3] if len(sys.argv) > 3 else "unpacked_pak"
        unpack_pak(pak_file, out_dir)
        print(f"Unpacked {pak_file} -> {out_dir}")
    elif cmd == "pack":
        src = sys.argv[2]
        out_pak = sys.argv[3] if len(sys.argv) > 3 else "output.pak"
        pack_pak(src, out_pak)
        print(f"Packed {src} -> {out_pak}")
    else:
        print(f"Unknown command '{cmd}'. Use 'pack' or 'unpack'.")
        sys.exit(1)
