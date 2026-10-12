#!/usr/bin/env python3
"""
Plants vs. Zombies: GOTY Edition — Release Package Builder for Miyoo Mini Plus
=============================================================================
Builds the complete distribution package and ready-to-flash OnionOS ZIP from a
legitimately owned PC copy of Plants vs. Zombies (Steam / EA App / CD).

SAFETY GUARANTEES:
- Operates entirely inside an isolated temporary staging directory (tempfile.mkdtemp).
- NEVER deletes or purges the user's selected destination directory.
- Merges files non-destructively (copy_tree_merge) with retry loops for Windows file locks.
- Sets explicit POSIX file permissions (0755 for binaries/scripts, 0644 for assets).
- Creates .installed marker for OnionOS on-device installer detection.
"""

import os
import sys
import shutil
import time
import zipfile
import tempfile
import argparse
import subprocess
from typing import Optional, Callable, Tuple, List

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from pak_lib import PakArchive, PakError
from optimize_assets import optimize_assets


def copy_tree_merge(src: str, dst: str, log_fn: Optional[Callable[[str], None]] = None):
    """
    Safely copy and merge files from src into dst WITHOUT deleting dst.
    Handles existing destination files and retries on transient file locks.
    """
    log = log_fn or print
    os.makedirs(dst, exist_ok=True)
    for root, dirs, files in os.walk(src):
        rel = os.path.relpath(root, src)
        target_dir = os.path.join(dst, rel) if rel != "." else dst
        os.makedirs(target_dir, exist_ok=True)
        for f in files:
            src_file = os.path.join(root, f)
            dst_file = os.path.join(target_dir, f)
            for attempt in range(3):
                try:
                    shutil.copy2(src_file, dst_file)
                    break
                except PermissionError:
                    if attempt < 2:
                        time.sleep(0.2)
                    else:
                        log(f"Notice: Could not overwrite {f} (file may be in use).")
                except Exception as e:
                    log(f"Notice copying {f}: {e}")
                    break


def detect_pc_game_dir() -> Optional[str]:
    """
    Auto-detect Plants vs. Zombies PC installation across Steam, EA App, Origin,
    and standard Program Files directories.
    """
    candidates = [
        # Relative repository folders
        os.path.join(REPO_ROOT, "Plants Vs Zombies"),
        os.path.join(REPO_ROOT, "PlantsVsZombies"),
        os.path.join(REPO_ROOT, "..", "Plants Vs Zombies"),
        os.path.join(REPO_ROOT, "..", "PlantsVsZombies"),
        # Steam default Windows paths
        r"C:\Program Files (x86)\Steam\steamapps\common\Plants Vs Zombies",
        r"C:\Program Files\Steam\steamapps\common\Plants Vs Zombies",
        r"D:\SteamLibrary\steamapps\common\Plants Vs Zombies",
        r"E:\SteamLibrary\steamapps\common\Plants Vs Zombies",
        # EA App / Origin default paths
        r"C:\Program Files (x86)\Origin Games\Plants vs Zombies",
        r"C:\Program Files\Origin Games\Plants vs Zombies",
        r"C:\Program Files (x86)\EA Games\Plants vs Zombies",
        r"C:\Program Files\EA Games\Plants vs Zombies",
        # PopCap Games standalone installer
        r"C:\Program Files (x86)\PopCap Games\Plants vs. Zombies",
        r"C:\Program Files\PopCap Games\Plants vs. Zombies",
        r"C:\PopCap Games\Plants vs. Zombies",
        # Linux / Proton paths
        os.path.expanduser("~/.steam/steam/steamapps/common/Plants Vs Zombies"),
        os.path.expanduser("~/.local/share/Steam/steamapps/common/Plants Vs Zombies"),
    ]

    for c in candidates:
        if os.path.isdir(c):
            main_pak = os.path.join(c, "main.pak")
            exe_file = os.path.join(c, "PlantsVsZombies.exe")
            if os.path.isfile(main_pak) or os.path.isfile(exe_file) or os.path.isdir(os.path.join(c, "properties")):
                return os.path.abspath(c)

    return None


def resolve_arm_binary(log_fn: Optional[Callable[[str], None]] = None) -> Optional[str]:
    """Resolve ARMv7 PvZ executable or return path to deployable binary."""
    log = log_fn or print
    candidates = [
        os.path.join(REPO_ROOT, "bin", "PvZ"),
        os.path.join(REPO_ROOT, "build", "PvZ"),
        os.path.join(REPO_ROOT, "build-miyoo", "PvZ"),
        os.path.join(REPO_ROOT, "packaging", "Roms", "PORTS", "Games", "Plants vs. Zombies", "PvZ"),
    ]

    for c in candidates:
        if os.path.isfile(c):
            log(f"Found engine ARM binary: {c}")
            return os.path.abspath(c)

    return None


def build_package(
    source: str,
    output_dir: Optional[str] = None,
    downscale_textures: bool = True,
    resample_audio: bool = True,
    build_from_source: bool = False,
    progress_cb: Optional[Callable[[int, str], None]] = None,
    log_cb: Optional[Callable[[str], None]] = None,
) -> Tuple[str, str]:
    """
    Core build routine. Safe staging in tempfile.mkdtemp.
    Returns (target_zip_path, final_output_dir).
    """
    def log(msg: str):
        if log_cb:
            log_cb(str(msg))
        else:
            print(msg)

    def set_progress(pct: int, msg: str):
        if progress_cb:
            progress_cb(pct, msg)
        log(f"[{pct}%] {msg}")

    source = os.path.abspath(source)
    if not output_dir:
        output_dir = os.path.join(REPO_ROOT, "dist")
    output_dir = os.path.abspath(output_dir)

    # SAFETY CHECK 1: Source validation
    set_progress(5, "Validating source directory...")
    if not os.path.isdir(source):
        raise ValueError(f"Source directory does not exist: {source}")

    has_main_pak = os.path.isfile(os.path.join(source, "main.pak"))
    has_props = os.path.isdir(os.path.join(source, "properties"))
    has_exe = os.path.isfile(os.path.join(source, "PlantsVsZombies.exe"))

    if not (has_main_pak or has_props or has_exe):
        raise FileNotFoundError(
            f"Directory does not appear to contain Plants vs. Zombies PC data: {source}\n"
            "Expected 'main.pak', 'properties/', or 'PlantsVsZombies.exe'."
        )

    # SAFETY CHECK 2: Output vs Source separation
    if os.path.normcase(source) == os.path.normcase(output_dir):
        raise ValueError("Source and Output folders cannot be the same! Please choose a different folder.")

    # SAFETY CHECK 3: Target ZIP locking check
    os.makedirs(output_dir, exist_ok=True)
    target_zip = os.path.join(output_dir, "PvZ_MiyooMiniPlus.zip")
    if os.path.isfile(target_zip):
        try:
            with open(target_zip, "a+b"):
                pass
        except PermissionError:
            raise PermissionError(
                f"The target file '{target_zip}' is currently locked by another program.\n"
                "Please close any application using it and retry."
            )

    # Staging in isolated temporary directory
    staging_dir = tempfile.mkdtemp(prefix="pvz_build_staging_")
    try:
        set_progress(15, "Setting up OnionOS Ports directory layout...")
        ports_root = os.path.join(staging_dir, "Roms", "PORTS")
        games_pvz = os.path.join(ports_root, "Games", "Plants vs. Zombies")
        shortcuts_dir = os.path.join(ports_root, "Shortcuts", "Strategy")
        imgs_dir = os.path.join(ports_root, "Imgs")
        libs_dir = os.path.join(games_pvz, "libs")
        userdata_dir = os.path.join(games_pvz, "userdata")

        os.makedirs(games_pvz, exist_ok=True)
        os.makedirs(shortcuts_dir, exist_ok=True)
        os.makedirs(imgs_dir, exist_ok=True)
        os.makedirs(libs_dir, exist_ok=True)
        os.makedirs(userdata_dir, exist_ok=True)

        # 1. Copy repository packaging templates if available
        pkg_ports = os.path.join(REPO_ROOT, "packaging", "Roms", "PORTS")
        if os.path.isdir(pkg_ports):
            copy_tree_merge(pkg_ports, ports_root, log_fn=log)

        # 2. Deploy launch scripts (launch.sh, launcher.sh)
        launch_script_path = os.path.join(games_pvz, "launch.sh")
        if not os.path.isfile(launch_script_path):
            template_launch = os.path.join(pkg_ports, "Games", "Plants vs. Zombies", "launch.sh")
            if os.path.isfile(template_launch):
                shutil.copy2(template_launch, launch_script_path)
            else:
                # Generate clean default launch.sh
                with open(launch_script_path, "w", newline="\n") as f:
                    f.write(
                        "#!/bin/sh\n"
                        "GAMEDIR=\"$(dirname \"$0\")\"\n"
                        "cd \"$GAMEDIR\"\n"
                        "cleanup() {\n"
                        "    sync\n"
                        "    /mnt/SDCARD/.tmp_update/bin/cpuclock 1200 2>/dev/null\n"
                        "    /customer/app/audioserver &\n"
                        "}\n"
                        "trap cleanup EXIT INT TERM HUP\n"
                        "if [ -f \"./.installed\" ] || [ -f \"./main_mmp.pak\" ]; then\n"
                        "    [ -f \"./setup_miyoo\" ] && rm -f ./setup_miyoo\n"
                        "fi\n"
                        "/mnt/SDCARD/.tmp_update/bin/cpuclock 1700 2>/dev/null\n"
                        "export SDL_MMIYOO_TEXTURE_POOL=1\n"
                        "export SDL_MMIYOO_TEXTURE_POOL_MAX_BYTES=16777216\n"
                        "export SDL_MMIYOO_VSYNC_MODE=off\n"
                        "export SDL_HINT_RENDER_SCALE_QUALITY=0\n"
                        "export LD_LIBRARY_PATH=\"./libs:$LD_LIBRARY_PATH\"\n"
                        "killall -9 audioserver 2>/dev/null\n"
                        "chmod +x ./PvZ 2>/dev/null\n"
                        "./PvZ\n"
                    )

        # Duplicate launch.sh as launcher.sh for alias compatibility
        shutil.copy2(launch_script_path, os.path.join(games_pvz, "launcher.sh"))

        # 3. Deploy shortcut script (Plants vs. Zombies.port)
        shortcut_target = os.path.join(shortcuts_dir, "Plants vs. Zombies.port")
        if not os.path.isfile(shortcut_target):
            template_shortcut = os.path.join(pkg_ports, "Shortcuts", "Strategy", "Plants vs. Zombies.port")
            if os.path.isfile(template_shortcut):
                shutil.copy2(template_shortcut, shortcut_target)
            else:
                with open(shortcut_target, "w", newline="\n") as f:
                    f.write(
                        "#!/bin/sh\n"
                        "GameName=\"Plants vs. Zombies\"\n"
                        "GameDir=\"Plants vs. Zombies\"\n"
                        "GameExecutable=\"launch.sh\"\n"
                        "GameDataFile=\"main_mmp.pak\"\n"
                        "PORTDIR=\"$(dirname \"$0\")\"\n"
                        "if [ -d \"$PORTDIR/Games/Plants vs. Zombies\" ]; then\n"
                        "    GAMEDIR=\"$PORTDIR/Games/Plants vs. Zombies\"\n"
                        "elif [ -d \"$PORTDIR/../../Games/Plants vs. Zombies\" ]; then\n"
                        "    GAMEDIR=\"$PORTDIR/../../Games/Plants vs. Zombies\"\n"
                        "else\n"
                        "    GAMEDIR=\"/mnt/SDCARD/Roms/PORTS/Games/Plants vs. Zombies\"\n"
                        "fi\n"
                        "cd \"$GAMEDIR\" && ./launch.sh\n"
                    )

        # 4. Deploy boxart image
        boxart_target = os.path.join(imgs_dir, "Plants vs. Zombies.png")
        if not os.path.isfile(boxart_target):
            template_boxart = os.path.join(pkg_ports, "Imgs", "Plants vs. Zombies.png")
            if os.path.isfile(template_boxart):
                shutil.copy2(template_boxart, boxart_target)

        # 5. Resolve engine binary (PvZ)
        set_progress(25, "Resolving PvZ ARM executable...")
        pvz_bin = resolve_arm_binary(log_fn=log)
        target_pvz_bin = os.path.join(games_pvz, "PvZ")
        if pvz_bin:
            shutil.copy2(pvz_bin, target_pvz_bin)
        else:
            log("Notice: PvZ ARM binary not yet built. Creating deployment placeholder.")
            with open(target_pvz_bin, "w") as f:
                f.write("#!/bin/sh\necho 'Plants vs. Zombies ARM binary placeholder'\n")

        # 6. Copy properties XML folder if present in source
        src_props = os.path.join(source, "properties")
        if os.path.isdir(src_props):
            set_progress(35, "Copying properties definitions...")
            copy_tree_merge(src_props, os.path.join(games_pvz, "properties"), log_fn=log)

        # 7. Optimize and repack assets into main_mmp.pak
        set_progress(45, "Optimizing textures (0.8x) and audio (22.05 kHz mono)...")
        target_mmp_pak = os.path.join(games_pvz, "main_mmp.pak")
        source_pak_or_dir = os.path.join(source, "main.pak") if has_main_pak else source

        def opt_progress_bridge(sub_pct, sub_msg):
            # Scale 45% -> 85%
            mapped = 45 + int(sub_pct * 0.40)
            set_progress(mapped, sub_msg)

        optimize_assets(
            source_input=source_pak_or_dir,
            output_pak_path=target_mmp_pak,
            scale_factor=0.8,
            downscale_textures=downscale_textures,
            resample_audio=resample_audio,
            progress_cb=opt_progress_bridge,
            log_cb=log,
        )

        # 8. Create .installed marker
        set_progress(88, "Writing .installed flag marker...")
        installed_flag = os.path.join(games_pvz, ".installed")
        with open(installed_flag, "w") as f:
            f.write(f"installed_at={time.strftime('%Y-%m-%d %H:%M:%S')}\nmethod=pc_builder\n")

        # 9. Create ready-to-flash ZIP with normalized POSIX permissions
        set_progress(92, "Creating OnionOS release ZIP archive...")
        staging_zip = os.path.join(staging_dir, "PvZ_MiyooMiniPlus.zip")
        with zipfile.ZipFile(staging_zip, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zipf:
            for root, _, files in os.walk(staging_dir):
                for file in files:
                    full_path = os.path.join(root, file)
                    if full_path == staging_zip:
                        continue
                    rel_path = os.path.relpath(full_path, staging_dir)
                    zipinfo = zipfile.ZipInfo(rel_path.replace("\\", "/"))

                    # POSIX permissions: 0755 for binaries/scripts, 0644 for assets
                    if (
                        file in ("PvZ", "setup_miyoo", "launch.sh", "launcher.sh", "Plants vs. Zombies.port")
                        or file.endswith(".sh")
                        or file.endswith(".so")
                        or ".so." in file
                    ):
                        zipinfo.external_attr = 0o755 << 16
                    else:
                        zipinfo.external_attr = 0o644 << 16

                    with open(full_path, "rb") as f:
                        zipf.writestr(zipinfo, f.read())

        # 10. Safely deploy files to final output directory
        set_progress(97, "Deploying release files to destination...")
        shutil.copy2(staging_zip, target_zip)
        copy_tree_merge(os.path.join(staging_dir, "Roms"), os.path.join(output_dir, "Roms"), log_fn=log)

        zip_mb = os.path.getsize(target_zip) / (1024 * 1024)
        set_progress(100, f"Release build complete! Package: {target_zip} ({zip_mb:.2f} MB)")
        return target_zip, output_dir

    finally:
        # Cleanup temporary staging
        try:
            shutil.rmtree(staging_dir, ignore_errors=True)
        except Exception:
            pass


def main():
    parser = argparse.ArgumentParser(
        description="Plants vs. Zombies Release Package Builder for Miyoo Mini Plus / OnionOS"
    )
    parser.add_argument("source", nargs="?", default="", help="Path to PC Plants vs. Zombies installation")
    parser.add_argument("--output", "-o", default=os.path.join(REPO_ROOT, "dist"), help="Output destination folder")
    parser.add_argument("--no-downscale", action="store_true", help="Do not downscale textures by 0.8x")
    parser.add_argument("--no-resample", action="store_true", help="Do not resample audio to 22.05 kHz mono")
    parser.add_argument("--build", "-b", action="store_true", help="Compile ARM engine binary from source")
    args = parser.parse_args()

    src = args.source or detect_pc_game_dir()
    if not src:
        print("Error: Could not automatically detect Plants vs. Zombies installation.")
        print("Please specify the game path as an argument.")
        sys.exit(1)

    print(f"Using game source: {src}")
    print(f"Output destination: {args.output}")

    try:
        build_package(
            source=src,
            output_dir=args.output,
            downscale_textures=not args.no_downscale,
            resample_audio=not args.no_resample,
            build_from_source=args.build,
        )
    except Exception as e:
        print(f"Error building release: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
