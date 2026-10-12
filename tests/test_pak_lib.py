#!/usr/bin/env python3
"""
Unit tests for PopCap PAK container library (tools/pak_lib.py).
Verifies:
- Header magic 0xBAC04AC0 and version 0
- File record table flags and FILEFLAGS_END (0x80)
- XOR 0xF7 byte encryption and decryption
- Byte-exact roundtrip packing and unpacking
- Linear seek operations and single-file access
- Path separator normalization and error handling
"""

import os
import sys
import struct
import tempfile
import unittest

# Ensure tools directory is in sys.path
TEST_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(TEST_DIR)
TOOLS_DIR = os.path.join(REPO_ROOT, "tools")
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

from pak_lib import (
    PakArchive,
    PakRecord,
    PakError,
    POP_PAK_MAGIC,
    POP_PAK_VERSION,
    FILEFLAGS_END,
    XOR_KEY,
    pack_pak,
    unpack_pak,
)
from optimize_assets import (
    convert_wav_to_22k_mono_pcm,
    downscale_image,
    optimize_assets,
)
from build_release import (
    build_package,
    copy_tree_merge,
    detect_pc_game_dir,
)


class TestPakLib(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.mkdtemp(prefix="pvz_test_pak_")

    def tearDown(self):
        import shutil
        shutil.rmtree(self.temp_dir, ignore_errors=True)

    def _create_dummy_png(self, w: int = 10, h: int = 10) -> bytes:
        import zlib
        lines = bytearray()
        for _ in range(h):
            lines.append(0)
            lines.extend([100, 150, 200, 255] * w)
        comp = zlib.compress(bytes(lines))
        ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
        ihdr_crc = struct.pack(">I", zlib.crc32(b"IHDR" + ihdr) & 0xFFFFFFFF)
        idat_len = struct.pack(">I", len(comp))
        idat_crc = struct.pack(">I", zlib.crc32(b"IDAT" + comp) & 0xFFFFFFFF)
        return (
            b"\x89PNG\r\n\x1a\n"
            + struct.pack(">I", 13)
            + b"IHDR"
            + ihdr
            + ihdr_crc
            + idat_len
            + b"IDAT"
            + comp
            + idat_crc
            + struct.pack(">I", 0)
            + b"IEND"
            + struct.pack(">I", zlib.crc32(b"IEND") & 0xFFFFFFFF)
        )

    def test_constants(self):
        """Verify PopCap PAK specification constants."""
        self.assertEqual(POP_PAK_MAGIC, 0xBAC04AC0)
        self.assertEqual(POP_PAK_VERSION, 0)
        self.assertEqual(FILEFLAGS_END, 0x80)
        self.assertEqual(XOR_KEY, 0xF7)

    def test_pak_header_and_structure(self):
        """Verify binary format of PAK header and record layout."""
        test_file_name = "properties\\resources.xml"
        test_content = b"<ResourceManifest><Version>1.0</Version></ResourceManifest>"
        pak_path = os.path.join(self.temp_dir, "test_header.pak")

        PakArchive.create_from_entries([(test_file_name, test_content)], pak_path)

        with open(pak_path, "rb") as f:
            magic, version = struct.unpack("<II", f.read(8))
            self.assertEqual(magic, POP_PAK_MAGIC)
            self.assertEqual(version, POP_PAK_VERSION)

            # Record table
            flag = f.read(1)[0]
            self.assertEqual(flag & FILEFLAGS_END, 0)

            name_len = f.read(1)[0]
            read_name = f.read(name_len).decode("latin-1")
            self.assertEqual(read_name, test_file_name)

            size, low_time, high_time = struct.unpack("<iII", f.read(12))
            self.assertEqual(size, len(test_content))

            # End of records table marker
            end_flag = f.read(1)[0]
            self.assertEqual(end_flag, FILEFLAGS_END)

            # File data payload: verify each byte is XORed with 0xF7
            payload = f.read(size)
            expected_payload = bytes(b ^ XOR_KEY for b in test_content)
            self.assertEqual(payload, expected_payload)

    def test_roundtrip_pack_and_unpack(self):
        """Verify byte-exact roundtrip of packed and unpacked files."""
        src_dir = os.path.join(self.temp_dir, "src_assets")
        os.makedirs(src_dir, exist_ok=True)

        files_to_create = {
            os.path.join("images", "Dave.png"): b"\x89PNG\r\n\x1a\nFakePngData1234567890",
            os.path.join("sounds", "groan.wav"): b"RIFF\x24\x00\x00\x00WAVEfmt FakeWavData",
            os.path.join("properties", "partner.xml"): b"<Partner>PopCap</Partner>",
            os.path.join("levels", "level1.xml"): b"<Level id=\"1\"><Zombies>10</Zombies></Level>",
            "root_file.txt": b"Hello Plants vs. Zombies on Miyoo Mini Plus!",
            "empty.bin": b"",
        }

        for rel_path, data in files_to_create.items():
            full_path = os.path.join(src_dir, rel_path)
            os.makedirs(os.path.dirname(full_path), exist_ok=True)
            with open(full_path, "wb") as f:
                f.write(data)

        pak_path = os.path.join(self.temp_dir, "main.pak")
        PakArchive.create_from_dir(src_dir, pak_path)
        self.assertTrue(os.path.isfile(pak_path))

        out_dir = os.path.join(self.temp_dir, "extracted")
        with PakArchive(pak_path) as archive:
            archive.extract_all(out_dir)

        # Verify all files match byte-for-byte
        for rel_path, expected_data in files_to_create.items():
            extracted_path = os.path.join(out_dir, rel_path)
            self.assertTrue(os.path.isfile(extracted_path), f"Missing extracted file: {rel_path}")
            with open(extracted_path, "rb") as f:
                extracted_data = f.read()
            self.assertEqual(extracted_data, expected_data, f"Data mismatch in {rel_path}")

    def test_linear_seek_and_unpack_file(self):
        """Verify linear seek and single-file retrieval without extracting full archive."""
        entries = [
            ("file_a.txt", b"Alpha content"),
            ("folder/file_b.txt", b"Beta content " * 100),
            ("folder/sub/file_c.bin", bytes(range(256))),
        ]
        pak_path = os.path.join(self.temp_dir, "seek_test.pak")
        PakArchive.create_from_entries(entries, pak_path)

        with PakArchive(pak_path) as archive:
            # Query files
            file_list = archive.list_files()
            self.assertEqual(len(file_list), 3)

            # Seek and read individual files in arbitrary order
            data_b = archive.read_file("folder\\file_b.txt")
            self.assertEqual(data_b, b"Beta content " * 100)

            data_a = archive.read_file("file_a.txt")
            self.assertEqual(data_a, b"Alpha content")

            # Forward slash query should also resolve
            data_c = archive.read_file("folder/sub/file_c.bin")
            self.assertEqual(data_c, bytes(range(256)))

            # Test unpack_file to disk
            dest_c = os.path.join(self.temp_dir, "disk_c.bin")
            archive.unpack_file("folder/sub/file_c.bin", dest_c)
            with open(dest_c, "rb") as f:
                self.assertEqual(f.read(), bytes(range(256)))

    def test_invalid_magic_raises_error(self):
        """Verify corrupted or invalid PAK file triggers PakError."""
        bad_pak = os.path.join(self.temp_dir, "bad.pak")
        with open(bad_pak, "wb") as f:
            f.write(b"NOT_POPCAP_MAGIC_1234567890")

        with self.assertRaises(PakError):
            PakArchive(bad_pak)

    def test_convenience_functions(self):
        """Verify pack_pak and unpack_pak convenience functions."""
        src_dir = os.path.join(self.temp_dir, "conv_src")
        os.makedirs(os.path.join(src_dir, "images"), exist_ok=True)
        img_path = os.path.join(src_dir, "images", "zombie.png")
        with open(img_path, "wb") as f:
            f.write(b"PNG_DATA_FOR_ZOMBIE")

        pak_path = os.path.join(self.temp_dir, "conv.pak")
        pack_pak(src_dir, pak_path)
        self.assertTrue(os.path.isfile(pak_path))

        out_dir = os.path.join(self.temp_dir, "conv_out")
        res = unpack_pak(pak_path, out_dir)
        self.assertTrue(res)
        extracted = os.path.join(out_dir, "images", "zombie.png")
        self.assertTrue(os.path.isfile(extracted))
        with open(extracted, "rb") as f:
            self.assertEqual(f.read(), b"PNG_DATA_FOR_ZOMBIE")

    def test_wav_audio_resampling(self):
        """Verify WAV audio downsampling to 22,050 Hz mono 16-bit PCM."""
        import io
        import wave

        # Generate a test 44,100 Hz stereo 16-bit WAV in memory
        orig_rate = 44100
        n_frames = 4410  # 0.1 second
        stereo_samples = []
        for i in range(n_frames):
            stereo_samples.extend([1000, -1000])  # left, right

        in_buf = io.BytesIO()
        with wave.open(in_buf, "wb") as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(orig_rate)
            w.writeframes(struct.pack(f"<{len(stereo_samples)}h", *stereo_samples))

        raw_wav = in_buf.getvalue()
        converted = convert_wav_to_22k_mono_pcm(raw_wav)

        with wave.open(io.BytesIO(converted), "rb") as out_wav:
            self.assertEqual(out_wav.getnchannels(), 1)
            self.assertEqual(out_wav.getsampwidth(), 2)
            self.assertEqual(out_wav.getframerate(), 22050)
            self.assertGreater(out_wav.getnframes(), 0)

    def test_texture_downscaling(self):
        """Verify PNG texture downscaling by 0.8x preserving dimensions."""
        import io
        import zlib

        # Construct a simple valid 50x50 RGBA PNG in memory
        w, h = 50, 50
        raw_scanlines = bytearray()
        for y in range(h):
            raw_scanlines.append(0)  # filter none
            for x in range(w):
                raw_scanlines.extend([255, 0, 128, 200])  # R, G, B, A

        compressed = zlib.compress(bytes(raw_scanlines))
        ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
        ihdr_crc = struct.pack(">I", zlib.crc32(b"IHDR" + ihdr) & 0xFFFFFFFF)
        idat_len = struct.pack(">I", len(compressed))
        idat_crc = struct.pack(">I", zlib.crc32(b"IDAT" + compressed) & 0xFFFFFFFF)

        png_buf = io.BytesIO()
        png_buf.write(b"\x89PNG\r\n\x1a\n")
        png_buf.write(struct.pack(">I", 13) + b"IHDR" + ihdr + ihdr_crc)
        png_buf.write(idat_len + b"IDAT" + compressed + idat_crc)
        png_buf.write(struct.pack(">I", 0) + b"IEND" + struct.pack(">I", zlib.crc32(b"IEND") & 0xFFFFFFFF))
        raw_png = png_buf.getvalue()

        # Downscale by 0.8x -> expected 40x40
        scaled_png = downscale_image(raw_png, scale_factor=0.8)
        self.assertTrue(len(scaled_png) > 8)
        self.assertEqual(scaled_png[:8], b"\x89PNG\r\n\x1a\n")

        # Read new dimensions from IHDR
        new_w, new_h = struct.unpack(">II", scaled_png[16:24])
        self.assertEqual(new_w, 40)
        self.assertEqual(new_h, 40)

    def test_optimize_assets_pipeline(self):
        """Verify full asset optimization pipeline into main_mmp.pak."""
        import io
        import wave
        import zlib

        stage_src = os.path.join(self.temp_dir, "opt_src")
        img_dir = os.path.join(stage_src, "images")
        snd_dir = os.path.join(stage_src, "sounds")
        os.makedirs(img_dir, exist_ok=True)
        os.makedirs(snd_dir, exist_ok=True)

        # 1. Write a 50x50 PNG
        w, h = 50, 50
        raw_scanlines = bytearray()
        for y in range(h):
            raw_scanlines.append(0)
            for x in range(w):
                raw_scanlines.extend([50, 100, 150, 255])
        compressed = zlib.compress(bytes(raw_scanlines))
        ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
        ihdr_crc = struct.pack(">I", zlib.crc32(b"IHDR" + ihdr) & 0xFFFFFFFF)
        idat_len = struct.pack(">I", len(compressed))
        idat_crc = struct.pack(">I", zlib.crc32(b"IDAT" + compressed) & 0xFFFFFFFF)
        with open(os.path.join(img_dir, "Peashooter.png"), "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n")
            f.write(struct.pack(">I", 13) + b"IHDR" + ihdr + ihdr_crc)
            f.write(idat_len + b"IDAT" + compressed + idat_crc)
            f.write(struct.pack(">I", 0) + b"IEND" + struct.pack(">I", zlib.crc32(b"IEND") & 0xFFFFFFFF))

        # 2. Write a 44.1kHz stereo WAV
        with wave.open(os.path.join(snd_dir, "splat.wav"), "wb") as w_file:
            w_file.setnchannels(2)
            w_file.setsampwidth(2)
            w_file.setframerate(44100)
            w_file.writeframes(struct.pack("<40h", *([500, -500] * 20)))

        # 3. Optimize and pack
        out_mmp_pak = os.path.join(self.temp_dir, "main_mmp.pak")
        stats = optimize_assets(stage_src, out_mmp_pak)

        self.assertTrue(os.path.isfile(out_mmp_pak))
        self.assertEqual(stats["images_optimized"], 1)
        self.assertEqual(stats["audio_optimized"], 1)

        # 4. Verify main_mmp.pak contents
        with PakArchive(out_mmp_pak) as archive:
            files = archive.list_files()
            self.assertIn("images\\Peashooter.png", files)
            self.assertIn("sounds\\splat.wav", files)

            # Check that Peashooter was downscaled to 40x40
            pea_data = archive.read_file("images\\Peashooter.png")
            new_w, new_h = struct.unpack(">II", pea_data[16:24])
            self.assertEqual(new_w, 40)
            self.assertEqual(new_h, 40)

            # Check that splat.wav was downsampled to 22.05 kHz mono
            wav_data = archive.read_file("sounds\\splat.wav")
            with wave.open(io.BytesIO(wav_data), "rb") as check_wav:
                self.assertEqual(check_wav.getnchannels(), 1)
                self.assertEqual(check_wav.getframerate(), 22050)

    def test_copy_tree_merge(self):
        """Verify copy_tree_merge merges non-destructively without purging destination."""
        src = os.path.join(self.temp_dir, "merge_src")
        dst = os.path.join(self.temp_dir, "merge_dst")
        os.makedirs(src, exist_ok=True)
        os.makedirs(dst, exist_ok=True)

        # Existing file in dst that must not be deleted
        pre_existing = os.path.join(dst, "pre_existing.txt")
        with open(pre_existing, "w") as f:
            f.write("preserve_me")

        # New file from src
        new_file = os.path.join(src, "new_file.txt")
        with open(new_file, "w") as f:
            f.write("new_content")

        copy_tree_merge(src, dst)

        self.assertTrue(os.path.isfile(pre_existing))
        with open(pre_existing, "r") as f:
            self.assertEqual(f.read(), "preserve_me")

        self.assertTrue(os.path.isfile(os.path.join(dst, "new_file.txt")))

    def test_build_package_release_tree_and_permissions(self):
        """Verify complete release package building, OnionOS tree, and POSIX permissions."""
        import zipfile

        # Setup mock PC source
        pc_src = os.path.join(self.temp_dir, "pc_goty_game")
        props_dir = os.path.join(pc_src, "properties")
        os.makedirs(props_dir, exist_ok=True)

        with open(os.path.join(props_dir, "resources.xml"), "w") as f:
            f.write("<Resources></Resources>")

        # Create a mock main.pak inside pc_src
        entries = [
            ("images/menu.png", self._create_dummy_png()),
            ("sounds/click.wav", b"RIFFfake_wav_data"),
        ]
        PakArchive.create_from_entries(entries, os.path.join(pc_src, "main.pak"))

        out_dir = os.path.join(self.temp_dir, "release_out")
        target_zip, final_dir = build_package(source=pc_src, output_dir=out_dir)

        self.assertTrue(os.path.isfile(target_zip))
        self.assertEqual(final_dir, os.path.abspath(out_dir))

        # Verify deployed OnionOS Roms directory
        deployed_pvz_dir = os.path.join(out_dir, "Roms", "PORTS", "Games", "Plants vs. Zombies")
        self.assertTrue(os.path.isdir(deployed_pvz_dir))
        self.assertTrue(os.path.isfile(os.path.join(deployed_pvz_dir, "main_mmp.pak")))
        self.assertTrue(os.path.isfile(os.path.join(deployed_pvz_dir, ".installed")))
        self.assertTrue(os.path.isfile(os.path.join(deployed_pvz_dir, "launch.sh")))
        self.assertTrue(os.path.isfile(os.path.join(deployed_pvz_dir, "launcher.sh")))

        deployed_shortcut = os.path.join(out_dir, "Roms", "PORTS", "Shortcuts", "Strategy", "Plants vs. Zombies.port")
        self.assertTrue(os.path.isfile(deployed_shortcut))

        # Inspect ZIP archive structure and POSIX permissions (external_attr)
        with zipfile.ZipFile(target_zip, "r") as z:
            namelist = z.namelist()
            self.assertIn("Roms/PORTS/Games/Plants vs. Zombies/main_mmp.pak", namelist)
            self.assertIn("Roms/PORTS/Games/Plants vs. Zombies/.installed", namelist)
            self.assertIn("Roms/PORTS/Games/Plants vs. Zombies/launch.sh", namelist)
            self.assertIn("Roms/PORTS/Shortcuts/Strategy/Plants vs. Zombies.port", namelist)

            # Check POSIX 0755 permissions for scripts/binaries
            launch_info = z.getinfo("Roms/PORTS/Games/Plants vs. Zombies/launch.sh")
            mode = (launch_info.external_attr >> 16) & 0o777
            self.assertEqual(mode, 0o755)

            port_info = z.getinfo("Roms/PORTS/Shortcuts/Strategy/Plants vs. Zombies.port")
            mode_port = (port_info.external_attr >> 16) & 0o777
            self.assertEqual(mode_port, 0o755)

            # Check POSIX 0644 permissions for assets
            asset_info = z.getinfo("Roms/PORTS/Games/Plants vs. Zombies/main_mmp.pak")
            mode_asset = (asset_info.external_attr >> 16) & 0o777
            self.assertEqual(mode_asset, 0o644)


if __name__ == "__main__":
    unittest.main()
