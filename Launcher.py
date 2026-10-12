#!/usr/bin/env python3
"""
Plants vs. Zombies — Graphical Port Launcher & Builder for Miyoo Mini Plus
==========================================================================
A lightweight, zero-dependency Tkinter GUI to select your original PC
Plants vs. Zombies folder, choose the output destination (or SD card),
optimize textures and audio, view live build progress, and generate a
ready-to-flash OnionOS package in seconds.

Requires Python standard library only (tkinter, ttk). Zero pip dependencies needed.
"""

import os
import sys
import threading
import platform
import traceback

import tkinter as tk
from tkinter import ttk, filedialog, messagebox, scrolledtext


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TOOLS_DIR = os.path.join(SCRIPT_DIR, "tools")
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

from build_release import (
    build_package,
    detect_pc_game_dir,
)


class PvZLauncherGUI(tk.Tk):
    """Zero-pip Tkinter GUI for Plants vs. Zombies Miyoo Mini Plus Release Builder."""

    def __init__(self):
        super().__init__()

        self.title("Plants vs. Zombies — Miyoo Mini Plus Port Builder")
        self.geometry("740x660")
        self.minsize(660, 560)

        # Apply clean theme if available
        self.style = ttk.Style(self)
        available_themes = self.style.theme_names()
        if "clam" in available_themes:
            self.style.theme_use("clam")

        self.is_building = False
        self.configure_styles()
        self.create_widgets()
        self.auto_detect_game()

    def configure_styles(self):
        self.style.configure("Title.TLabel", font=("Helvetica", 14, "bold"))
        self.style.configure("Subtitle.TLabel", font=("Helvetica", 9), foreground="#444444")
        self.style.configure("Header.TLabel", font=("Helvetica", 10, "bold"))
        self.style.configure("Status.TLabel", font=("Helvetica", 9, "bold"))
        self.style.configure("Build.TButton", font=("Helvetica", 11, "bold"), padding=8)
        self.style.configure("Action.TButton", font=("Helvetica", 9), padding=4)

    def create_widgets(self):
        main_frame = ttk.Frame(self, padding="15")
        main_frame.pack(fill=tk.BOTH, expand=True)

        # Header Banner
        header_frame = ttk.Frame(main_frame)
        header_frame.pack(fill=tk.X, pady=(0, 12))

        title_lbl = ttk.Label(
            header_frame,
            text="🌻 Plants vs. Zombies — Miyoo Mini Plus Port Builder",
            style="Title.TLabel",
        )
        title_lbl.pack(anchor=tk.W)

        subtitle_lbl = ttk.Label(
            header_frame,
            text="Extract, optimize (0.8x textures & 22kHz audio), and package your PC GOTY copy for OnionOS.",
            style="Subtitle.TLabel",
        )
        subtitle_lbl.pack(anchor=tk.W, pady=(2, 0))

        # Section 1: PC Game Path
        sec1 = ttk.LabelFrame(main_frame, text=" 1. Original PC Game Folder ", padding="10")
        sec1.pack(fill=tk.X, pady=(0, 10))

        lbl_s = ttk.Label(
            sec1,
            text="Select your PC Plants vs. Zombies directory (containing 'main.pak' or 'properties/'):",
        )
        lbl_s.pack(anchor=tk.W, pady=(0, 4))

        f_s = ttk.Frame(sec1)
        f_s.pack(fill=tk.X)

        self.var_source = tk.StringVar()
        self.entry_source = ttk.Entry(f_s, textvariable=self.var_source)
        self.entry_source.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 6))

        btn_browse_s = ttk.Button(f_s, text="Browse...", style="Action.TButton", command=self.browse_source)
        btn_browse_s.pack(side=tk.LEFT, padx=(0, 4))

        btn_detect = ttk.Button(f_s, text="Auto-Detect", style="Action.TButton", command=self.auto_detect_game)
        btn_detect.pack(side=tk.LEFT)

        # Section 2: Output Destination / SD Card
        sec2 = ttk.LabelFrame(main_frame, text=" 2. Output Destination / MicroSD Card ", padding="10")
        sec2.pack(fill=tk.X, pady=(0, 10))

        lbl_o = ttk.Label(
            sec2,
            text="Select destination folder (e.g., dist/ or your MicroSD card root drive E:\\):",
        )
        lbl_o.pack(anchor=tk.W, pady=(0, 4))

        f_o = ttk.Frame(sec2)
        f_o.pack(fill=tk.X)

        default_dist = os.path.join(SCRIPT_DIR, "dist")
        self.var_output = tk.StringVar(value=default_dist)
        self.entry_output = ttk.Entry(f_o, textvariable=self.var_output)
        self.entry_output.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 6))

        btn_browse_o = ttk.Button(f_o, text="Browse...", style="Action.TButton", command=self.browse_output)
        btn_browse_o.pack(side=tk.LEFT)

        # Section 3: Optimization Options
        sec3 = ttk.LabelFrame(main_frame, text=" 3. Optimization Settings (MMP Hardware Match) ", padding="10")
        sec3.pack(fill=tk.X, pady=(0, 10))

        self.var_downscale = tk.BooleanVar(value=True)
        chk_downscale = ttk.Checkbutton(
            sec3,
            text="Downscale textures by 0.8x (Matches native 640x480 screen, saves ~30MB RAM)",
            variable=self.var_downscale,
        )
        chk_downscale.pack(anchor=tk.W, pady=(0, 2))

        self.var_resample = tk.BooleanVar(value=True)
        chk_resample = ttk.Checkbutton(
            sec3,
            text="Downsample audio SFX to 22.05 kHz mono 16-bit PCM (Eliminates audio stuttering)",
            variable=self.var_resample,
        )
        chk_resample.pack(anchor=tk.W)

        # Section 4: Progress Bar & Status
        sec4 = ttk.Frame(main_frame)
        sec4.pack(fill=tk.X, pady=(0, 8))

        f_status = ttk.Frame(sec4)
        f_status.pack(fill=tk.X, pady=(0, 4))

        self.var_status = tk.StringVar(value="Status: Ready to build")
        lbl_status = ttk.Label(f_status, textvariable=self.var_status, style="Status.TLabel")
        lbl_status.pack(side=tk.LEFT)

        self.var_pct = tk.StringVar(value="0%")
        lbl_pct = ttk.Label(f_status, textvariable=self.var_pct, font=("Helvetica", 9, "bold"))
        lbl_pct.pack(side=tk.RIGHT)

        self.progress_bar = ttk.Progressbar(sec4, orient="horizontal", mode="determinate")
        self.progress_bar.pack(fill=tk.X)

        # Section 5: Build Button
        self.btn_build = ttk.Button(
            main_frame,
            text="⚡ Build & Export to SD Card",
            style="Build.TButton",
            command=self.start_build,
        )
        self.btn_build.pack(fill=tk.X, pady=(6, 10))

        # Section 6: Console Log Viewer
        sec6 = ttk.LabelFrame(main_frame, text=" Build Console Output ", padding="8")
        sec6.pack(fill=tk.BOTH, expand=True)

        self.txt_log = scrolledtext.ScrolledText(
            sec6,
            wrap=tk.WORD,
            height=8,
            font=("Consolas", 8),
            bg="#1e1e1e",
            fg="#d4d4d4",
            insertbackground="#ffffff",
        )
        self.txt_log.pack(fill=tk.BOTH, expand=True)

    def log(self, message: str):
        """Append message to GUI log widget."""
        def _append():
            self.txt_log.insert(tk.END, message + "\n")
            self.txt_log.see(tk.END)
        self.after(0, _append)

    def set_progress(self, percentage: int, status_text: str):
        """Update progress bar and status text safely from background thread."""
        def _update():
            self.progress_bar["value"] = percentage
            self.var_pct.set(f"{percentage}%")
            self.var_status.set(f"Status: {status_text}")
        self.after(0, _update)

    def browse_source(self):
        path = filedialog.askdirectory(title="Select PC Plants vs. Zombies Game Folder")
        if path:
            self.var_source.set(os.path.normpath(path))

    def browse_output(self):
        path = filedialog.askdirectory(title="Select Output Destination / SD Card")
        if path:
            self.var_output.set(os.path.normpath(path))

    def auto_detect_game(self):
        detected = detect_pc_game_dir()
        if detected:
            self.var_source.set(detected)
            self.log(f"Auto-detected Plants vs. Zombies installation: {detected}")
            self.var_status.set("Status: PC installation auto-detected!")
        else:
            self.log("Auto-detection: No default Steam or EA App folder found. Please browse manually.")

    def set_ui_state(self, enabled: bool):
        state = tk.NORMAL if enabled else tk.DISABLED
        self.entry_source.config(state=state)
        self.entry_output.config(state=state)
        self.btn_build.config(state=state)

    def start_build(self):
        if self.is_building:
            return

        source = self.var_source.get().strip()
        output = self.var_output.get().strip()

        if not source:
            messagebox.showerror("Error", "Please select your PC Plants vs. Zombies game folder.")
            return

        if not os.path.isdir(source):
            messagebox.showerror("Error", f"Source directory does not exist:\n{source}")
            return

        if not output:
            messagebox.showerror("Error", "Please select an output destination.")
            return

        self.is_building = True
        self.set_ui_state(False)
        self.set_progress(0, "Starting build process...")

        thread = threading.Thread(
            target=self._run_build_thread,
            args=(source, output, self.var_downscale.get(), self.var_resample.get()),
            daemon=True,
        )
        thread.start()

    def _run_build_thread(self, source: str, output: str, downscale: bool, resample: bool):
        try:
            target_zip, final_output = build_package(
                source=source,
                output_dir=output,
                downscale_textures=downscale,
                resample_audio=resample,
                progress_cb=self.set_progress,
                log_cb=self.log,
            )

            def _on_success():
                self.is_building = False
                self.set_ui_state(True)
                messagebox.showinfo(
                    "Success!",
                    f"Release package built successfully!\n\n"
                    f"ZIP: {os.path.basename(target_zip)}\n"
                    f"Folder: {final_output}\n\n"
                    "You can now copy the generated 'Roms' folder directly to your MicroSD card root!",
                )
            self.after(0, _on_success)

        except Exception as e:
            err_msg = str(e)
            tb_str = traceback.format_exc()

            def _on_error(msg=err_msg, tb=tb_str):
                self.is_building = False
                self.set_ui_state(True)
                self.set_progress(0, f"Error: {msg}")
                self.log(f"\n[ERROR] Build failed: {msg}\n{tb}\n")
                messagebox.showerror("Build Error", f"An error occurred during packaging:\n\n{msg}\n\nDetails:\n{tb}")

            self.after(0, _on_error)



def main():
    # If run in terminal with CLI args, delegate to build_release.py
    if len(sys.argv) > 1 and sys.argv[1] not in ("--gui", "-g"):
        from build_release import main as cli_main
        cli_main()
        return

    app = PvZLauncherGUI()
    app.mainloop()


if __name__ == "__main__":
    main()
