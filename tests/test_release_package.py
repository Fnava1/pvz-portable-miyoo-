#!/usr/bin/env python3
"""
Unit and Integration Tests for Plants vs. Zombies Release Package & Memory Audit
================================================================================
Validates:
- Static memory profiling audit against SigmaStar SSD202D hardware budget (<= 85 MB).
- OnionOS Pure Ports release directory staging (Roms/PORTS/...).
- Distribution ZIP generation (PvZ_MiyooMiniPlus_v1.0.0.zip).
- POSIX permissions in ZIP archive (0755 for executables/scripts, 0644 for assets).
- Clean-room BYOD legal compliance (zero .exe or copyrighted assets).
- Script integrity and lifecycle metadata.
"""

import os
import sys
import stat
import shutil
import zipfile
import tempfile
import unittest

# Discover repo root and scripts directory
TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(TESTS_DIR)
SCRIPTS_DIR = os.path.join(REPO_ROOT, "scripts")
if SCRIPTS_DIR not in sys.path:
    sys.path.insert(0, SCRIPTS_DIR)

from package_release import (
    audit_memory_budget,
    stage_release,
    create_release_zip,
    package_release,
    verify_clean_room,
    MEMORY_BUDGET_TABLE,
    BASE_STEADY_STATE_MB,
    MAX_STEADY_STATE_MB,
    HARD_CEILING_MB,
)


class TestReleasePackage(unittest.TestCase):
    """Test suite for release packaging, memory profiling audit, and OnionOS conformance."""

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp(prefix="pvz_test_pkg_")

    def tearDown(self):
        if os.path.isdir(self.temp_dir):
            shutil.rmtree(self.temp_dir, ignore_errors=True)

    def test_memory_budget_audit_constraints(self):
        """Verify steady-state memory budget is <= 78 MB and ceiling is <= 85 MB."""
        report = audit_memory_budget()

        # Audit status
        self.assertTrue(report["audit_passed"], "Memory audit must pass all constraints")

        # Verify subsystem budgets
        subsystems = report["subsystems"]
        self.assertEqual(subsystems["textures"]["budget_mb"], 24.0)
        self.assertEqual(subsystems["reanim"]["budget_mb"], 8.0)
        self.assertEqual(subsystems["music_stream"]["budget_mb"], 2.0)
        self.assertEqual(subsystems["sfx_cache"]["budget_mb"], 8.0)
        self.assertEqual(subsystems["logic_and_fb"]["budget_mb"], 15.0)

        # Base steady-state should sum to 57.0 MB
        self.assertEqual(report["base_steady_state_mb"], 57.0)
        self.assertLessEqual(report["base_steady_state_mb"], 60.0)

        # Peak wave surge + base should sum to 78.0 MB (<= 78.0 MB requirement)
        self.assertEqual(report["max_steady_state_mb"], 78.0)
        self.assertLessEqual(report["max_steady_state_mb"], 78.0)

        # Hard memory ceiling should be <= 85.0 MB
        self.assertEqual(report["hard_ceiling_mb"], 85.0)
        self.assertLessEqual(report["hard_ceiling_mb"], 85.0)

        # Headroom before OOM killer
        self.assertGreaterEqual(report["safety_margin_mb"], 7.0)
        self.assertLessEqual(report["hard_ceiling_mb"], report["usable_system_ram_mb"])

    def test_stage_release_structure(self):
        """Verify staging creates the correct Pure Ports hierarchy with all required files."""
        build_dir = os.path.join(REPO_ROOT, "build")
        allow_missing = not os.path.isfile(os.path.join(build_dir, "PvZ"))

        roms_path = stage_release(
            output_dir=self.temp_dir,
            version="1.0.0",
            build_dir=build_dir,
            allow_missing_binaries=allow_missing,
        )

        self.assertTrue(os.path.isdir(roms_path))

        # Check required Pure Ports paths
        game_dir = os.path.join(roms_path, "PORTS", "Games", "Plants vs. Zombies")
        img_file = os.path.join(roms_path, "PORTS", "Imgs", "Plants vs. Zombies.png")
        shortcut_file = os.path.join(roms_path, "PORTS", "Shortcuts", "Strategy", "Plants vs. Zombies.port")

        self.assertTrue(os.path.isfile(img_file), "Boxart image must be staged in Roms/PORTS/Imgs/")
        self.assertTrue(os.path.isfile(shortcut_file), "Pure Ports shortcut must be staged in Shortcuts/Strategy/")

        expected_game_files = [
            "PvZ",
            "setup_miyoo",
            "launch.sh",
            "launcher.sh",
            "HOW_TO_INSTALL.txt",
        ]
        for f in expected_game_files:
            p = os.path.join(game_dir, f)
            self.assertTrue(os.path.isfile(p), f"Required file '{f}' must exist in {game_dir}")

        expected_dirs = ["libs", "userdata", "properties"]
        for d in expected_dirs:
            dp = os.path.join(game_dir, d)
            self.assertTrue(os.path.isdir(dp), f"Required directory '{d}' must exist in {game_dir}")
            keep = os.path.join(dp, ".gitkeep")
            self.assertTrue(os.path.isfile(keep), f".gitkeep placeholder must exist in {dp}")

    def test_release_zip_creation_and_entry_paths(self):
        """Verify release ZIP contains properly formatted Roms/ paths and no self-inclusion."""
        build_dir = os.path.join(REPO_ROOT, "build")
        allow_missing = not os.path.isfile(os.path.join(build_dir, "PvZ"))

        zip_path, staged_roms, audit = package_release(
            output_dir=self.temp_dir,
            version="1.0.0",
            build_dir=build_dir,
            allow_missing_binaries=allow_missing,
            run_memory_audit=False,
        )

        self.assertTrue(os.path.isfile(zip_path), f"ZIP package {zip_path} must exist")
        self.assertGreater(os.path.getsize(zip_path), 1000, "ZIP package must not be empty")

        with zipfile.ZipFile(zip_path, "r") as zf:
            namelist = zf.namelist()

            # Every entry must start with Roms/
            for name in namelist:
                self.assertTrue(
                    name.startswith("Roms/"),
                    f"Entry '{name}' in release ZIP does not start with 'Roms/'",
                )
                self.assertFalse(
                    name.endswith(".zip"),
                    f"Archive must not contain recursive ZIP files: '{name}'",
                )

            # Key entries must be present
            self.assertIn("Roms/PORTS/Imgs/Plants vs. Zombies.png", namelist)
            self.assertIn("Roms/PORTS/Shortcuts/Strategy/Plants vs. Zombies.port", namelist)
            self.assertIn("Roms/PORTS/Games/Plants vs. Zombies/PvZ", namelist)
            self.assertIn("Roms/PORTS/Games/Plants vs. Zombies/setup_miyoo", namelist)
            self.assertIn("Roms/PORTS/Games/Plants vs. Zombies/launch.sh", namelist)
            self.assertIn("Roms/PORTS/Games/Plants vs. Zombies/launcher.sh", namelist)
            self.assertIn("Roms/PORTS/Games/Plants vs. Zombies/HOW_TO_INSTALL.txt", namelist)

            # Runtime markers and game archives must NOT be present in clean distribution
            self.assertNotIn("Roms/PORTS/Games/Plants vs. Zombies/.installed", namelist)
            self.assertNotIn("Roms/PORTS/Games/Plants vs. Zombies/main.pak", namelist)
            self.assertNotIn("Roms/PORTS/Games/Plants vs. Zombies/main_mmp.pak", namelist)

    def test_posix_permissions_in_zip(self):
        """Verify normalized POSIX permissions inside the release ZIP archive."""
        build_dir = os.path.join(REPO_ROOT, "build")
        allow_missing = not os.path.isfile(os.path.join(build_dir, "PvZ"))

        zip_path, _, _ = package_release(
            output_dir=self.temp_dir,
            version="1.0.0",
            build_dir=build_dir,
            allow_missing_binaries=allow_missing,
            run_memory_audit=False,
        )

        executables_755 = {
            "Roms/PORTS/Games/Plants vs. Zombies/PvZ",
            "Roms/PORTS/Games/Plants vs. Zombies/setup_miyoo",
            "Roms/PORTS/Games/Plants vs. Zombies/launch.sh",
            "Roms/PORTS/Games/Plants vs. Zombies/launcher.sh",
            "Roms/PORTS/Shortcuts/Strategy/Plants vs. Zombies.port",
        }

        data_644 = {
            "Roms/PORTS/Imgs/Plants vs. Zombies.png",
            "Roms/PORTS/Games/Plants vs. Zombies/HOW_TO_INSTALL.txt",
            "Roms/PORTS/Games/Plants vs. Zombies/libs/.gitkeep",
            "Roms/PORTS/Games/Plants vs. Zombies/userdata/.gitkeep",
            "Roms/PORTS/Games/Plants vs. Zombies/properties/.gitkeep",
        }

        with zipfile.ZipFile(zip_path, "r") as zf:
            for info in zf.infolist():
                # Ensure UNIX system code is set
                self.assertEqual(
                    info.create_system, 3,
                    f"ZIP entry {info.filename} must have UNIX system code (3)",
                )

                mode = (info.external_attr >> 16) & 0o777

                if info.filename in executables_755:
                    self.assertEqual(
                        mode, 0o755,
                        f"Executable '{info.filename}' must have POSIX permission 0755, got {oct(mode)}",
                    )
                elif info.filename in data_644:
                    self.assertEqual(
                        mode, 0o644,
                        f"Data file '{info.filename}' must have POSIX permission 0644, got {oct(mode)}",
                    )
                elif info.filename.endswith("/"):
                    self.assertEqual(
                        mode, 0o755,
                        f"Directory entry '{info.filename}' must have permission 0755, got {oct(mode)}",
                    )

    def test_clean_room_byod_enforcement(self):
        """Enforce zero copyrighted files (.pak, .dat, .mo3) and zero Windows .exe files."""
        # 1. Staged release should verify clean
        build_dir = os.path.join(REPO_ROOT, "build")
        allow_missing = not os.path.isfile(os.path.join(build_dir, "PvZ"))

        stage_release(
            output_dir=self.temp_dir,
            version="1.0.0",
            build_dir=build_dir,
            allow_missing_binaries=allow_missing,
        )
        # Should not raise
        verify_clean_room(self.temp_dir)

        # 2. Injecting a forbidden file must raise ValueError
        fake_exe = os.path.join(self.temp_dir, "Roms", "PORTS", "Games", "Plants vs. Zombies", "unmo3.exe")
        with open(fake_exe, "wb") as f:
            f.write(b"MZfakeexe")

        with self.assertRaises(ValueError):
            verify_clean_room(self.temp_dir)

        os.remove(fake_exe)

        fake_pak = os.path.join(self.temp_dir, "Roms", "PORTS", "Games", "Plants vs. Zombies", "main.pak")
        with open(fake_pak, "wb") as f:
            f.write(b"fakepak")

        with self.assertRaises(ValueError):
            verify_clean_room(self.temp_dir)

    def test_launch_script_and_shortcut_integrity(self):
        """Verify launch script contains required OnionOS lifecycle resilience routines."""
        build_dir = os.path.join(REPO_ROOT, "build")
        allow_missing = not os.path.isfile(os.path.join(build_dir, "PvZ"))

        roms_path = stage_release(
            output_dir=self.temp_dir,
            version="1.0.0",
            build_dir=build_dir,
            allow_missing_binaries=allow_missing,
        )

        launch_sh = os.path.join(roms_path, "PORTS", "Games", "Plants vs. Zombies", "launch.sh")
        with open(launch_sh, "r") as f:
            content = f.read()

        self.assertIn("trap cleanup EXIT INT TERM HUP", content)
        self.assertIn("sync", content)
        self.assertIn("cpuclock 1200", content)
        self.assertIn("CPU_FREQ=1800", content)
        self.assertIn("CPU_FREQ=1400", content)
        self.assertIn("cpuclock $CPU_FREQ", content)
        self.assertIn("SDL_MMIYOO_TEXTURE_POOL=1", content)
        self.assertIn("killall -9 audioserver", content)
        self.assertIn("setup_miyoo", content)

        shortcut_port = os.path.join(roms_path, "PORTS", "Shortcuts", "Strategy", "Plants vs. Zombies.port")
        with open(shortcut_port, "r") as f:
            port_content = f.read()

        self.assertIn('GameName="Plants vs. Zombies"', port_content)
        self.assertIn('GameExecutable="launch.sh"', port_content)
        self.assertIn("launch.sh", port_content)


if __name__ == "__main__":
    unittest.main()
