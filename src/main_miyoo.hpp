#ifndef PVZ_MAIN_MIYOO_HPP
#define PVZ_MAIN_MIYOO_HPP

/**
 * Resets framebuffer panning offsets (vinfo.xoffset = 0, vinfo.yoffset = 0)
 * via ioctl(fb, FBIOPAN_DISPLAY, &vinfo) on /dev/fb0.
 * Eliminates post-game black screen upon returning to OnionOS MainUI.
 */
void ResetFramebuffer();

#endif // PVZ_MAIN_MIYOO_HPP
