#ifndef PVZ_MAIN_MIYOO_HPP
#define PVZ_MAIN_MIYOO_HPP

/**
 * Resets framebuffer panning offsets (vinfo.xoffset = 0, vinfo.yoffset = 0)
 * via ioctl(fb, FBIOPAN_DISPLAY, &vinfo) on /dev/fb0.
 * Eliminates post-game black screen upon returning to OnionOS MainUI.
 */
void ResetFramebuffer();

/**
 * Detects safe target CPU clock in MHz per hardware model:
 * 1800 MHz for Miyoo Mini Plus, 1400 MHz for Miyoo Mini non-Plus.
 */
int DetectTargetCpuMhz();

/**
 * Restores safe CPU governor and model-specific target clock frequency
 * directly via POSIX file descriptors (zero fork/system overhead).
 */
void RestoreCpuClockSafe(int targetCpuMhz);

#endif // PVZ_MAIN_MIYOO_HPP
