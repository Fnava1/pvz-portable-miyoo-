#!/bin/sh
# ==============================================================================
# Plants vs. Zombies for Miyoo Mini Plus (OnionOS)
# Native 640x480 port with 0.8x asset optimization & lifecycle resilience
# ==============================================================================

GAMEDIR="$(dirname "$0")"
cd "$GAMEDIR"

# Discard console output to eliminate SD card I/O wear and flash write latency
exec > /dev/null 2>&1
rm -f ./launch.log 2>/dev/null


# ------------------------------------------------------------------------------
# Lifecycle Traps & Power Resiliency
# ------------------------------------------------------------------------------
cleanup() {
    # 1. Flush dirty filesystem buffers to SD card to prevent theme/FAT corruption
    sync

    # 2. Restore safe CPU clock to 1200 MHz
    if [ -f "/mnt/SDCARD/.tmp_update/bin/cpuclock" ]; then
        /mnt/SDCARD/.tmp_update/bin/cpuclock 1200 2>/dev/null
    elif command -v cpuclock >/dev/null 2>&1; then
        cpuclock 1200 2>/dev/null
    fi

    # 3. Restore CPU governor to ondemand
    if [ -f "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor" ]; then
        echo ondemand > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
    fi

    # 4. Restart OnionOS audioserver
    if [ -x "/customer/app/audioserver" ]; then
        /customer/app/audioserver &
    elif command -v audioserver >/dev/null 2>&1; then
        audioserver &
    fi

    # 5. Purge any leftover diagnostic log files
    rm -f ./launch.log 2>/dev/null
}
trap cleanup EXIT INT TERM HUP

# ------------------------------------------------------------------------------
# Autonomous On-Device Asset Detection & Converter Self-Cleanup
# ------------------------------------------------------------------------------
if [ -f "./main_mmp.pak" ]; then
    # Converted asset package present:
    # Remove setup_miyoo if present to save storage space on microSD card
    if [ -f "./setup_miyoo" ]; then
        rm -f ./setup_miyoo
    fi
    touch .installed
elif [ -f "./main.pak" ]; then
    # Only raw PC main.pak detected: run on-device converter first
    if [ -f "./setup_miyoo" ]; then
        chmod +x ./setup_miyoo
        ./setup_miyoo
        if [ $? -eq 0 ] && [ -f "./main_mmp.pak" ]; then
            rm -f ./setup_miyoo
            touch .installed
        else
            echo "setup_miyoo conversion failed!"
            exit 1
        fi
    else
        echo "Error: setup_miyoo missing and main_mmp.pak not found!"
        exit 1
    fi
fi

# ------------------------------------------------------------------------------
# Performance & Subsystem Configuration
# ------------------------------------------------------------------------------
# 1. Dynamic Hardware Detection & Safe Overclocking
# Miyoo Mini Plus: 1800 MHz (AXP223 PMIC, large battery, WiFi present)
# Miyoo Mini (v1/v2/v3/v4 non-Plus): 1400 MHz (conservative ceiling avoiding PMIC panic)
IS_PLUS=0
if [ -d /sys/class/net/wlan0 ] || grep -qi "plus" /proc/cmdline 2>/dev/null; then
    IS_PLUS=1
fi

if [ "$IS_PLUS" -eq 1 ]; then
    CPU_FREQ=1800
else
    CPU_FREQ=1400
fi

if [ -f "/mnt/SDCARD/.tmp_update/bin/cpuclock" ]; then
    /mnt/SDCARD/.tmp_update/bin/cpuclock $CPU_FREQ 2>/dev/null
elif command -v cpuclock >/dev/null 2>&1; then
    cpuclock $CPU_FREQ 2>/dev/null
fi

# 2. Hardware texture pool & blitter tuning
export SDL_MMIYOO_TEXTURE_POOL=1
export SDL_MMIYOO_TEXTURE_POOL_MAX_BYTES=16777216
export SDL_MMIYOO_VSYNC_MODE=adaptive
export PVZ_SMOOTH_SCALE=1
export SDL_HINT_RENDER_SCALE_QUALITY=0
export LD_LIBRARY_PATH="$GAMEDIR/libs:/mnt/SDCARD/Roms/PORTS/Games/Plants vs. Zombies/libs:./libs:$LD_LIBRARY_PATH"

# 3. Audio device isolation: kill audioserver to prevent MI_AO hardware conflict
killall -9 audioserver 2>/dev/null

# ------------------------------------------------------------------------------
# Execute Game Binary
# ------------------------------------------------------------------------------
echo "Files in game directory:"
ls -la .
echo "Checking executable ./PvZ:"
ls -la ./PvZ
chmod +x ./PvZ 2>/dev/null
echo "Launching ./PvZ..."
./PvZ "$@"
PvZ_EXIT=$?
echo "PvZ terminated with exit code: $PvZ_EXIT"


# Cleanup trap automatically executes upon script exit or signal
