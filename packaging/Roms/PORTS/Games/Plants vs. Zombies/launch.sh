#!/bin/sh
# Plants vs. Zombies for Miyoo Mini Plus (OnionOS)
# Native 640x480 port with 0.8x asset optimization

GAMEDIR="$(dirname "$0")"
cd "$GAMEDIR"

# Dynamic CPU clock setting & cleanup trap
cleanup() {
    sync
    /mnt/SDCARD/.tmp_update/bin/cpuclock 1200 2>/dev/null
    /customer/app/audioserver &
}
trap cleanup EXIT INT TERM HUP

# Check for smart PC conversion detection / on-device setup
if [ -f "./.installed" ] || [ -f "./main_mmp.pak" ]; then
    # PC conversion complete: remove on-device setup tool if present to save SD card space
    if [ -f "./setup_miyoo" ]; then
        rm -f ./setup_miyoo
    fi
elif [ -f "./main.pak" ]; then
    # Raw PC data detected, run on-device converter if available
    if [ -f "./setup_miyoo" ]; then
        chmod +x ./setup_miyoo
        ./setup_miyoo
        if [ $? -eq 0 ]; then
            rm -f ./setup_miyoo
            touch .installed
        else
            echo "setup_miyoo failed!"
            exit 1
        fi
    fi
fi

# Set safe clock target for MM+ (1700 MHz via OnionOS cpuclock)
/mnt/SDCARD/.tmp_update/bin/cpuclock 1700 2>/dev/null

# Hardware texture pool & blitter tuning
export SDL_MMIYOO_TEXTURE_POOL=1
export SDL_MMIYOO_TEXTURE_POOL_MAX_BYTES=16777216
export SDL_MMIYOO_VSYNC_MODE=off
export SDL_HINT_RENDER_SCALE_QUALITY=0
export LD_LIBRARY_PATH="./libs:$LD_LIBRARY_PATH"

# Audio isolation: kill audioserver to prevent MI_AO conflict
killall -9 audioserver 2>/dev/null

# Run the game binary
chmod +x ./PvZ 2>/dev/null
./PvZ

# Teardown handled by trap cleanup
