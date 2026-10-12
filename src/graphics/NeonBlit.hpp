#ifndef PVZ_GRAPHICS_NEONBLIT_HPP
#define PVZ_GRAPHICS_NEONBLIT_HPP

#include <cstddef>
#include <cstdint>

namespace PvZ {
namespace Graphics {

enum BlitMode {
    BLIT_COPY = 0,
    BLIT_ALPHA = 1,
    BLIT_ADDITIVE = 2
};

/**
 * Returns true if ARM NEON SIMD vectorization was enabled at compile time.
 */
bool IsNeonSupported();

/**
 * Fast solid fill of 32-bit ARGB pixels.
 */
void NeonBlit_Fill_ARGB(uint32_t* dst, size_t count, uint32_t color);

/**
 * Vector alpha blend (standard src-over blend):
 *   dst = (src * src_a + dst * (255 - src_a)) / 255
 * When NEON is available, processes 8 pixels per loop in NEON registers
 * with zero VFP/integer bank crossing penalties.
 * Otherwise falls back to an exact scalar integer pipeline.
 */
void NeonBlit_AlphaBlend_ARGB(uint32_t* dst, const uint32_t* src, size_t count);

/**
 * Vector additive blend (used for PopCap particle glows, sun, fire):
 *   dst = min(255, dst + (src * src_a) / 255)
 * Saturated vector additions in 8-pixel batches.
 */
void NeonBlit_AdditiveBlend_ARGB(uint32_t* dst, const uint32_t* src, size_t count);

/**
 * Vector alpha blend with tint color modulation:
 *   src_tinted = (src * tint) / 255
 *   dst = (src_tinted * src_a + dst * (255 - src_a)) / 255
 */
void NeonBlit_AlphaBlend_Tint_ARGB(uint32_t* dst, const uint32_t* src, size_t count, uint32_t tint_color);

/**
 * Vector additive blend with tint color modulation:
 *   src_tinted = (src * tint) / 255
 *   dst = min(255, dst + (src_tinted * src_a) / 255)
 */
void NeonBlit_AdditiveBlend_Tint_ARGB(uint32_t* dst, const uint32_t* src, size_t count, uint32_t tint_color);

/**
 * Convert 32-bit ARGB8888 pixels to 16-bit RGB565 format (native to SSD202D /fb0).
 */
void NeonBlit_RGB565_From_ARGB(uint16_t* dst, const uint32_t* src, size_t count);

/**
 * General span blending dispatcher with mode and tint support.
 */
void NeonBlit_Span(uint32_t* dst, const uint32_t* src, size_t count, BlitMode mode, uint32_t tint_color = 0xFFFFFFFFu);

} // namespace Graphics
} // namespace PvZ

#endif // PVZ_GRAPHICS_NEONBLIT_HPP
