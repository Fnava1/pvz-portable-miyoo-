#!/usr/bin/env python3
"""
Plants vs. Zombies Asset Optimizer for Miyoo Mini Plus
======================================================
Transforms game assets for native Miyoo Mini Plus execution:
1. Downscales PNG/JPEG textures by 0.8x (640x480 match), preserving alpha and aspect ratio.
2. Converts WAV sound effects to 22,050 Hz, mono, 16-bit signed PCM.
3. Repacks assets into optimized PopCap PAK archive (main_mmp.pak).

Supports Pillow (PIL), PySide6/OpenCV, and pure Python fallbacks (zlib/wave/struct).
"""

import os
import sys
import io
import wave
import struct
import zlib
from typing import Optional, Callable, Dict, Any, Tuple, List

# Ensure tools directory is in sys.path
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from pak_lib import PakArchive, PakError

# Check available image backends
HAVE_PIL = False
try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:
    HAVE_PIL = False

HAVE_PYSIDE6 = False
if not HAVE_PIL:
    try:
        from PySide6.QtGui import QImage
        from PySide6.QtCore import QBuffer, QIODevice, Qt
        HAVE_PYSIDE6 = True
    except ImportError:
        HAVE_PYSIDE6 = False


# ============================================================================
# Audio Optimization: 22,050 Hz Mono 16-bit PCM Resampler (Pure Python)
# ============================================================================

def convert_wav_to_22k_mono_pcm(wav_bytes: bytes) -> bytes:
    """
    Converts raw WAV audio bytes into standard 22,050 Hz mono 16-bit signed PCM.
    Pure Python standard library implementation using wave and struct.
    """
    try:
        with wave.open(io.BytesIO(wav_bytes), "rb") as in_wav:
            nchannels = in_wav.getnchannels()
            sampwidth = in_wav.getsampwidth()
            framerate = in_wav.getframerate()
            nframes = in_wav.getnframes()
            comptype = in_wav.getcomptype()

            # Pass through if already 22050 Hz mono 16-bit PCM
            if nchannels == 1 and sampwidth == 2 and framerate == 22050 and comptype == "NONE":
                return wav_bytes

            if comptype != "NONE" or nframes == 0:
                # Compressed or zero-frame WAV; safe fallback
                return wav_bytes

            raw_frames = in_wav.readframes(nframes)

        # Unpack samples into normalized float range [-1.0, 1.0]
        samples_per_frame = nchannels
        total_samples = nframes * samples_per_frame

        if sampwidth == 1:
            # 8-bit unsigned PCM (0..255)
            raw_vals = struct.unpack(f"{total_samples}B", raw_frames)
            float_samples = [(v - 128) / 128.0 for v in raw_vals]
        elif sampwidth == 2:
            # 16-bit signed PCM
            raw_vals = struct.unpack(f"<{total_samples}h", raw_frames)
            float_samples = [v / 32768.0 for v in raw_vals]
        elif sampwidth == 3:
            # 24-bit signed PCM
            float_samples = []
            for i in range(0, len(raw_frames), 3):
                v = int.from_bytes(raw_frames[i : i + 3], byteorder="little", signed=True)
                float_samples.append(v / 8388608.0)
        elif sampwidth == 4:
            # 32-bit signed PCM
            raw_vals = struct.unpack(f"<{total_samples}i", raw_frames)
            float_samples = [v / 2147483648.0 for v in raw_vals]
        else:
            return wav_bytes

        # Downmix to mono
        if nchannels == 2:
            mono_samples = []
            for i in range(0, len(float_samples), 2):
                l = float_samples[i]
                r = float_samples[i + 1] if i + 1 < len(float_samples) else l
                mono_samples.append(0.5 * (l + r))
        elif nchannels == 1:
            mono_samples = float_samples
        else:
            # Multi-channel downmix
            mono_samples = []
            for i in range(0, len(float_samples), nchannels):
                frame = float_samples[i : i + nchannels]
                mono_samples.append(sum(frame) / len(frame))

        # Resample to 22,050 Hz via linear interpolation
        target_rate = 22050
        if framerate != target_rate:
            ratio = target_rate / float(framerate)
            target_length = max(1, int(round(len(mono_samples) * ratio)))
            resampled = []
            max_idx = len(mono_samples) - 1
            for j in range(target_length):
                pos = j * (max_idx / max(1, target_length - 1)) if target_length > 1 else 0.0
                idx0 = int(pos)
                idx1 = min(idx0 + 1, max_idx)
                frac = pos - idx0
                v = (1.0 - frac) * mono_samples[idx0] + frac * mono_samples[idx1]
                resampled.append(v)
            mono_samples = resampled

        # Quantize to 16-bit signed PCM
        pcm16_data = []
        for s in mono_samples:
            clamped = max(-32768, min(32767, int(round(s * 32767.0))))
            pcm16_data.append(clamped)

        out_buffer = io.BytesIO()
        with wave.open(out_buffer, "wb") as out_wav:
            out_wav.setnchannels(1)
            out_wav.setsampwidth(2)
            out_wav.setframerate(target_rate)
            packed_frames = struct.pack(f"<{len(pcm16_data)}h", *pcm16_data)
            out_wav.writeframes(packed_frames)

        return out_buffer.getvalue()

    except Exception:
        # If any parsing error occurs, gracefully preserve original sound
        return wav_bytes


# ============================================================================
# Image Optimization: 0.8x Texture Downscaler (PIL -> PySide6 -> Pure Python)
# ============================================================================

def _downscale_pil(img_bytes: bytes, scale_factor: float = 0.8) -> Optional[bytes]:
    """Downscale image using PIL/Pillow."""
    try:
        with Image.open(io.BytesIO(img_bytes)) as img:
            fmt = img.format or "PNG"
            w, h = img.size
            new_w = max(1, int(round(w * scale_factor)))
            new_h = max(1, int(round(h * scale_factor)))

            if new_w == w and new_h == h:
                return img_bytes

            # Preserve transparency / alpha channels
            if img.mode in ("P", "PA"):
                img = img.convert("RGBA")

            resample_filter = getattr(Image, "Resampling", Image).BILINEAR
            resized = img.resize((new_w, new_h), resample=resample_filter)

            out_buf = io.BytesIO()
            if fmt.upper() in ("JPEG", "JPG"):
                if resized.mode != "RGB":
                    resized = resized.convert("RGB")
                resized.save(out_buf, format="JPEG", quality=85, optimize=True)
            else:
                resized.save(out_buf, format="PNG", optimize=True)

            return out_buf.getvalue()
    except Exception:
        return None


def _downscale_pyside6(img_bytes: bytes, scale_factor: float = 0.8) -> Optional[bytes]:
    """Downscale image using PySide6 QImage."""
    try:
        qimg = QImage()
        if not qimg.loadFromData(img_bytes):
            return None

        w = qimg.width()
        h = qimg.height()
        new_w = max(1, int(round(w * scale_factor)))
        new_h = max(1, int(round(h * scale_factor)))

        if new_w == w and new_h == h:
            return img_bytes

        scaled = qimg.scaled(
            new_w, new_h, Qt.IgnoreAspectRatio, Qt.SmoothTransformation
        )

        buffer = QBuffer()
        buffer.open(QIODevice.WriteOnly)
        fmt = "PNG"
        if img_bytes[:2] == b"\xff\xd8":
            fmt = "JPEG"
        scaled.save(buffer, fmt)
        data = bytes(buffer.data())
        buffer.close()
        return data
    except Exception:
        return None


def _downscale_pure_python_png(img_bytes: bytes, scale_factor: float = 0.8) -> Optional[bytes]:
    """
    Pure Python PNG decoder and bilinear resizer using zlib and struct.
    Handles standard 8-bit truecolor (RGB / RGBA) non-interlaced PNG files.
    """
    try:
        if len(img_bytes) < 8 or img_bytes[:8] != b"\x89PNG\r\n\x1a\n":
            return None

        pos = 8
        ihdr = None
        idat_chunks = []

        while pos < len(img_bytes):
            chunk_len = struct.unpack(">I", img_bytes[pos : pos + 4])[0]
            chunk_type = img_bytes[pos + 4 : pos + 8]
            chunk_data = img_bytes[pos + 8 : pos + 8 + chunk_len]
            pos += 12 + chunk_len

            if chunk_type == b"IHDR":
                ihdr = chunk_data
            elif chunk_type == b"IDAT":
                idat_chunks.append(chunk_data)
            elif chunk_type == b"IEND":
                break

        if not ihdr or not idat_chunks:
            return None

        w, h, bit_depth, color_type, compression, filter_method, interlace = struct.unpack(
            ">IIBBBBB", ihdr
        )

        # Support 8-bit depth, RGB (2) or RGBA (6), non-interlaced
        if bit_depth != 8 or interlace != 0 or color_type not in (2, 6):
            return None

        channels = 4 if color_type == 6 else 3
        raw_decompressed = zlib.decompress(b"".join(idat_chunks))

        # Reconstruct scanlines
        stride = w * channels
        expected_len = h * (stride + 1)
        if len(raw_decompressed) < expected_len:
            return None

        pixels = bytearray(h * stride)
        in_offset = 0

        for y in range(h):
            filter_type = raw_decompressed[in_offset]
            in_offset += 1
            line_in = raw_decompressed[in_offset : in_offset + stride]
            in_offset += stride

            out_y_offset = y * stride
            prev_y_offset = (y - 1) * stride if y > 0 else None

            if filter_type == 0:  # None
                pixels[out_y_offset : out_y_offset + stride] = line_in
            elif filter_type == 1:  # Sub
                for x in range(stride):
                    left = pixels[out_y_offset + x - channels] if x >= channels else 0
                    pixels[out_y_offset + x] = (line_in[x] + left) & 0xFF
            elif filter_type == 2:  # Up
                for x in range(stride):
                    up = pixels[prev_y_offset + x] if prev_y_offset is not None else 0
                    pixels[out_y_offset + x] = (line_in[x] + up) & 0xFF
            elif filter_type == 3:  # Average
                for x in range(stride):
                    left = pixels[out_y_offset + x - channels] if x >= channels else 0
                    up = pixels[prev_y_offset + x] if prev_y_offset is not None else 0
                    pixels[out_y_offset + x] = (line_in[x] + ((left + up) >> 1)) & 0xFF
            elif filter_type == 4:  # Paeth
                for x in range(stride):
                    a = pixels[out_y_offset + x - channels] if x >= channels else 0
                    b = pixels[prev_y_offset + x] if prev_y_offset is not None else 0
                    c = (
                        pixels[prev_y_offset + x - channels]
                        if (prev_y_offset is not None and x >= channels)
                        else 0
                    )
                    p = a + b - c
                    pa = abs(p - a)
                    pb = abs(p - b)
                    pc = abs(p - c)
                    pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                    pixels[out_y_offset + x] = (line_in[x] + pr) & 0xFF
            else:
                return None

        # Bilinear downscaling
        new_w = max(1, int(round(w * scale_factor)))
        new_h = max(1, int(round(h * scale_factor)))
        new_stride = new_w * channels
        out_pixels = bytearray(new_h * (new_stride + 1))

        for ny in range(new_h):
            sy = ny * (h - 1) / max(1, new_h - 1) if new_h > 1 else 0.0
            y0 = int(sy)
            y1 = min(y0 + 1, h - 1)
            fy = sy - y0

            out_row_start = ny * (new_stride + 1)
            out_pixels[out_row_start] = 0  # Filter type 0 (None)

            for nx in range(new_w):
                sx = nx * (w - 1) / max(1, new_w - 1) if new_w > 1 else 0.0
                x0 = int(sx)
                x1 = min(x0 + 1, w - 1)
                fx = sx - x0

                px_out = out_row_start + 1 + nx * channels
                for c_idx in range(channels):
                    p00 = pixels[y0 * stride + x0 * channels + c_idx]
                    p10 = pixels[y0 * stride + x1 * channels + c_idx]
                    p01 = pixels[y1 * stride + x0 * channels + c_idx]
                    p11 = pixels[y1 * stride + x1 * channels + c_idx]

                    val = (
                        (1.0 - fx) * (1.0 - fy) * p00
                        + fx * (1.0 - fy) * p10
                        + (1.0 - fx) * fy * p01
                        + fx * fy * p11
                    )
                    out_pixels[px_out + c_idx] = max(0, min(255, int(round(val))))

        # Compress new PNG
        compressed_idat = zlib.compress(bytes(out_pixels), level=6)

        new_ihdr = struct.pack(">IIBBBBB", new_w, new_h, 8, color_type, 0, 0, 0)
        ihdr_crc = struct.pack(">I", zlib.crc32(b"IHDR" + new_ihdr) & 0xFFFFFFFF)
        idat_len = struct.pack(">I", len(compressed_idat))
        idat_crc = struct.pack(">I", zlib.crc32(b"IDAT" + compressed_idat) & 0xFFFFFFFF)

        out_png = io.BytesIO()
        out_png.write(b"\x89PNG\r\n\x1a\n")
        out_png.write(struct.pack(">I", 13) + b"IHDR" + new_ihdr + ihdr_crc)
        out_png.write(idat_len + b"IDAT" + compressed_idat + idat_crc)
        out_png.write(struct.pack(">I", 0) + b"IEND" + struct.pack(">I", zlib.crc32(b"IEND") & 0xFFFFFFFF))

        return out_png.getvalue()
    except Exception:
        return None


def downscale_image(img_bytes: bytes, scale_factor: float = 0.8) -> bytes:
    """
    Downscale texture by scale_factor (default 0.8x = 640x480 match).
    Attempts Pillow first, then PySide6, then pure Python PNG handler,
    falling back to original bytes if unsupported.
    """
    if HAVE_PIL:
        res = _downscale_pil(img_bytes, scale_factor)
        if res is not None:
            return res

    if HAVE_PYSIDE6:
        res = _downscale_pyside6(img_bytes, scale_factor)
        if res is not None:
            return res

    # Pure Python PNG resizer fallback
    res = _downscale_pure_python_png(img_bytes, scale_factor)
    if res is not None:
        return res

    # Graceful fallback preserving original texture
    return img_bytes


# ============================================================================
# Asset Pipeline: High-level Optimizer & Pack Creator
# ============================================================================

def optimize_assets(
    source_input: str,
    output_pak_path: str,
    scale_factor: float = 0.8,
    downscale_textures: bool = True,
    resample_audio: bool = True,
    progress_cb: Optional[Callable[[int, str], None]] = None,
    log_cb: Optional[Callable[[str], None]] = None,
) -> Dict[str, Any]:
    """
    Process assets from source (directory or main.pak) and emit main_mmp.pak.
    """
    def log(msg: str):
        if log_cb:
            log_cb(msg)
        else:
            print(msg)

    def set_progress(pct: int, msg: str):
        if progress_cb:
            progress_cb(pct, msg)
        log(f"[{pct}%] {msg}")

    source_input = os.path.abspath(source_input)
    output_pak_path = os.path.abspath(output_pak_path)

    set_progress(5, "Scanning input assets...")

    raw_items: List[Tuple[str, bytes]] = []

    # Check if input is a PAK file or a directory
    if os.path.isfile(source_input) and source_input.lower().endswith(".pak"):
        log(f"Loading input PAK archive: {source_input}")
        with PakArchive(source_input) as archive:
            files = archive.list_files()
            for name in files:
                raw_items.append((name, archive.read_file(name)))
    elif os.path.isdir(source_input):
        # Look for main.pak inside directory
        inner_pak = os.path.join(source_input, "main.pak")
        if os.path.isfile(inner_pak):
            log(f"Found main.pak in directory: {inner_pak}")
            with PakArchive(inner_pak) as archive:
                files = archive.list_files()
                for name in files:
                    raw_items.append((name, archive.read_file(name)))
        else:
            log(f"Scanning loose asset files in: {source_input}")
            for root, _, files in os.walk(source_input):
                for f in sorted(files):
                    full = os.path.join(root, f)
                    rel = os.path.relpath(full, source_input)
                    with open(full, "rb") as fp:
                        raw_items.append((rel, fp.read()))
    else:
        raise FileNotFoundError(f"Input source not found: {source_input}")

    total_files = len(raw_items)
    if total_files == 0:
        raise ValueError(f"No assets found in source: {source_input}")

    set_progress(15, f"Optimizing {total_files} asset files...")

    images_count = 0
    audio_count = 0
    orig_total_bytes = 0
    opt_total_bytes = 0

    optimized_entries: List[Tuple[str, bytes]] = []

    for i, (rel_path, data) in enumerate(raw_items):
        orig_len = len(data)
        orig_total_bytes += orig_len
        lower_path = rel_path.lower()
        norm_path = lower_path.replace("\\", "/")

        # Preserve font sprite sheets, data/, and caustic assets at 1:1 original resolution
        # Bitmap font descriptors (.txt) rely on exact pixel bounding rectangles.
        # Pool caustics rely on 256x256 lookup tables.
        is_preserved_asset = (
            norm_path.startswith("data/") or
            norm_path.startswith("fonts/") or
            "/fonts/" in norm_path or
            "font" in os.path.basename(norm_path) or
            "caustic" in norm_path
        )

        if downscale_textures and not is_preserved_asset and (lower_path.endswith(".png") or lower_path.endswith(".jpg") or lower_path.endswith(".jpeg")):
            new_data = downscale_image(data, scale_factor=scale_factor)
            images_count += 1
        elif resample_audio and lower_path.endswith(".wav"):
            new_data = convert_wav_to_22k_mono_pcm(data)
            audio_count += 1
        else:
            new_data = data

        opt_total_bytes += len(new_data)
        optimized_entries.append((rel_path, new_data))

        if i % 25 == 0 or i == total_files - 1:
            pct = 15 + int(65 * (i + 1) / total_files)
            set_progress(pct, f"Processed {i + 1}/{total_files} files...")

    set_progress(85, "Packing optimized assets into main_mmp.pak...")
    PakArchive.create_from_entries(optimized_entries, output_pak_path)

    set_progress(100, f"Successfully created {os.path.basename(output_pak_path)}")

    orig_mb = orig_total_bytes / (1024 * 1024)
    opt_mb = opt_total_bytes / (1024 * 1024)
    saved_pct = (1.0 - (opt_total_bytes / max(1, orig_total_bytes))) * 100.0

    stats = {
        "total_files": total_files,
        "images_optimized": images_count,
        "audio_optimized": audio_count,
        "original_mb": orig_mb,
        "optimized_mb": opt_mb,
        "savings_percent": saved_pct,
        "output_path": output_pak_path,
    }

    log(
        f"Optimization complete: {images_count} images, {audio_count} audio files. "
        f"{orig_mb:.2f} MB -> {opt_mb:.2f} MB ({saved_pct:.1f}% space saved)"
    )
    return stats


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python optimize_assets.py <source_dir_or_main.pak> <output_main_mmp.pak>")
        sys.exit(1)

    src = sys.argv[1]
    dst = sys.argv[2]
    optimize_assets(src, dst)
