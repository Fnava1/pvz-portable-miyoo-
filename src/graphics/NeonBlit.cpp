#include "graphics/NeonBlit.hpp"

#include <algorithm>
#include <cstring>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define PVZ_HAS_NEON 1
#else
#define PVZ_HAS_NEON 0
#endif

namespace PvZ {
namespace Graphics {

bool IsNeonSupported() {
#if PVZ_HAS_NEON
    return true;
#else
    return false;
#endif
}

static inline uint8_t div255_scalar(uint32_t val) {
    return static_cast<uint8_t>((val + (val >> 8) + 1) >> 8);
}

void NeonBlit_Fill_ARGB(uint32_t* dst, size_t count, uint32_t color) {
    if (!dst || count == 0) return;

#if PVZ_HAS_NEON
    uint32x4_t c4 = vdupq_n_u32(color);
    while (count >= 8) {
        vst1q_u32(dst, c4);
        vst1q_u32(dst + 4, c4);
        dst += 8;
        count -= 8;
    }
#endif

    while (count > 0) {
        *dst++ = color;
        count--;
    }
}

void NeonBlit_AlphaBlend_ARGB(uint32_t* dst, const uint32_t* src, size_t count) {
    if (!dst || !src || count == 0) return;

#if PVZ_HAS_NEON
    // If both pointers have matching misalignment phase, peel 1 pixel with scalar to align to 8-byte boundary
    if (((reinterpret_cast<uintptr_t>(dst) & 7) == (reinterpret_cast<uintptr_t>(src) & 7)) &&
        ((reinterpret_cast<uintptr_t>(dst) & 7) != 0) && count > 0) {
        uint32_t s_px = *src++;
        uint8_t sa = static_cast<uint8_t>((s_px >> 24) & 0xFF);
        if (sa == 0) {
            dst++;
        } else if (sa == 255) {
            *dst++ = s_px;
        } else {
            uint32_t d_px = *dst;
            uint8_t inv_a = static_cast<uint8_t>(255 - sa);
            uint8_t sr = static_cast<uint8_t>((s_px >> 16) & 0xFF);
            uint8_t sg = static_cast<uint8_t>((s_px >> 8) & 0xFF);
            uint8_t sb = static_cast<uint8_t>(s_px & 0xFF);

            uint8_t da = static_cast<uint8_t>((d_px >> 24) & 0xFF);
            uint8_t dr = static_cast<uint8_t>((d_px >> 16) & 0xFF);
            uint8_t dg = static_cast<uint8_t>((d_px >> 8) & 0xFF);
            uint8_t db = static_cast<uint8_t>(d_px & 0xFF);

            uint8_t out_r = div255_scalar(sr * sa + dr * inv_a);
            uint8_t out_g = div255_scalar(sg * sa + dg * inv_a);
            uint8_t out_b = div255_scalar(sb * sa + db * inv_a);
            uint8_t out_a = div255_scalar(255 * sa + da * inv_a);

            *dst++ = (static_cast<uint32_t>(out_a) << 24) |
                     (static_cast<uint32_t>(out_r) << 16) |
                     (static_cast<uint32_t>(out_g) << 8)  |
                     static_cast<uint32_t>(out_b);
        }
        count--;
    }

    if (((reinterpret_cast<uintptr_t>(dst) | reinterpret_cast<uintptr_t>(src)) & 7) == 0) {
        while (count >= 8) {
            uint8x8x4_t s = vld4_u8(reinterpret_cast<const uint8_t*>(src));
            uint8x8x4_t d = vld4_u8(reinterpret_cast<const uint8_t*>(dst));
            uint8x8_t inv_a = vsub_u8(vdup_n_u8(255), s.val[3]);

            uint8x8x4_t out;
            for (int k = 0; k < 3; ++k) {
                uint16x8_t prod = vmull_u8(s.val[k], s.val[3]);
                prod = vmlal_u8(prod, d.val[k], inv_a);
                uint16x8_t temp = vaddq_u16(prod, vshrq_n_u16(prod, 8));
                temp = vaddq_u16(temp, vdupq_n_u16(1));
                out.val[k] = vshrn_n_u16(temp, 8);
            }

            // Alpha channel
            uint16x8_t a_prod = vmull_u8(s.val[3], vdup_n_u8(255));
            a_prod = vmlal_u8(a_prod, d.val[3], inv_a);
            uint16x8_t a_temp = vaddq_u16(a_prod, vshrq_n_u16(a_prod, 8));
            a_temp = vaddq_u16(a_temp, vdupq_n_u16(1));
            out.val[3] = vshrn_n_u16(a_temp, 8);

            vst4_u8(reinterpret_cast<uint8_t*>(dst), out);

            src += 8;
            dst += 8;
            count -= 8;
        }
    }
#endif

    while (count > 0) {
        uint32_t s_px = *src++;
        uint8_t sa = static_cast<uint8_t>((s_px >> 24) & 0xFF);
        if (sa == 0) {
            dst++;
            count--;
            continue;
        }

        uint32_t d_px = *dst;
        if (sa == 255) {
            *dst++ = s_px;
            count--;
            continue;
        }

        uint8_t inv_a = static_cast<uint8_t>(255 - sa);
        uint8_t sr = static_cast<uint8_t>((s_px >> 16) & 0xFF);
        uint8_t sg = static_cast<uint8_t>((s_px >> 8) & 0xFF);
        uint8_t sb = static_cast<uint8_t>(s_px & 0xFF);

        uint8_t da = static_cast<uint8_t>((d_px >> 24) & 0xFF);
        uint8_t dr = static_cast<uint8_t>((d_px >> 16) & 0xFF);
        uint8_t dg = static_cast<uint8_t>((d_px >> 8) & 0xFF);
        uint8_t db = static_cast<uint8_t>(d_px & 0xFF);

        uint8_t out_r = div255_scalar(sr * sa + dr * inv_a);
        uint8_t out_g = div255_scalar(sg * sa + dg * inv_a);
        uint8_t out_b = div255_scalar(sb * sa + db * inv_a);
        uint8_t out_a = div255_scalar(255 * sa + da * inv_a);

        *dst++ = (static_cast<uint32_t>(out_a) << 24) |
                 (static_cast<uint32_t>(out_r) << 16) |
                 (static_cast<uint32_t>(out_g) << 8)  |
                 static_cast<uint32_t>(out_b);
        count--;
    }
}

void NeonBlit_AdditiveBlend_ARGB(uint32_t* dst, const uint32_t* src, size_t count) {
    if (!dst || !src || count == 0) return;

#if PVZ_HAS_NEON
    while (count >= 8) {
        uint8x8x4_t s = vld4_u8(reinterpret_cast<const uint8_t*>(src));
        uint8x8x4_t d = vld4_u8(reinterpret_cast<const uint8_t*>(dst));

        uint8x8x4_t out;
        for (int k = 0; k < 3; ++k) {
            uint16x8_t prod = vmull_u8(s.val[k], s.val[3]);
            uint16x8_t temp = vaddq_u16(prod, vshrq_n_u16(prod, 8));
            temp = vaddq_u16(temp, vdupq_n_u16(1));
            uint8x8_t add_term = vshrn_n_u16(temp, 8);
            out.val[k] = vqadd_u8(d.val[k], add_term);
        }
        out.val[3] = vqadd_u8(d.val[3], s.val[3]);

        vst4_u8(reinterpret_cast<uint8_t*>(dst), out);

        src += 8;
        dst += 8;
        count -= 8;
    }
#endif

    while (count > 0) {
        uint32_t s_px = *src++;
        uint8_t sa = static_cast<uint8_t>((s_px >> 24) & 0xFF);
        if (sa == 0) {
            dst++;
            count--;
            continue;
        }

        uint32_t d_px = *dst;
        uint8_t sr = static_cast<uint8_t>((s_px >> 16) & 0xFF);
        uint8_t sg = static_cast<uint8_t>((s_px >> 8) & 0xFF);
        uint8_t sb = static_cast<uint8_t>(s_px & 0xFF);

        uint8_t da = static_cast<uint8_t>((d_px >> 24) & 0xFF);
        uint8_t dr = static_cast<uint8_t>((d_px >> 16) & 0xFF);
        uint8_t dg = static_cast<uint8_t>((d_px >> 8) & 0xFF);
        uint8_t db = static_cast<uint8_t>(d_px & 0xFF);

        uint8_t add_r = div255_scalar(sr * sa);
        uint8_t add_g = div255_scalar(sg * sa);
        uint8_t add_b = div255_scalar(sb * sa);

        uint8_t out_r = static_cast<uint8_t>(std::min(255, dr + add_r));
        uint8_t out_g = static_cast<uint8_t>(std::min(255, dg + add_g));
        uint8_t out_b = static_cast<uint8_t>(std::min(255, db + add_b));
        uint8_t out_a = static_cast<uint8_t>(std::min(255, da + sa));

        *dst++ = (static_cast<uint32_t>(out_a) << 24) |
                 (static_cast<uint32_t>(out_r) << 16) |
                 (static_cast<uint32_t>(out_g) << 8)  |
                 static_cast<uint32_t>(out_b);
        count--;
    }
}

void NeonBlit_AlphaBlend_Tint_ARGB(uint32_t* dst, const uint32_t* src, size_t count, uint32_t tint_color) {
    if (!dst || !src || count == 0) return;
    if (tint_color == 0xFFFFFFFFu) {
        NeonBlit_AlphaBlend_ARGB(dst, src, count);
        return;
    }

#if PVZ_HAS_NEON
    if (((reinterpret_cast<uintptr_t>(dst) & 7) == (reinterpret_cast<uintptr_t>(src) & 7)) &&
        ((reinterpret_cast<uintptr_t>(dst) & 7) != 0) && count > 0) {
        uint8_t tr_p = static_cast<uint8_t>((tint_color >> 16) & 0xFF);
        uint8_t tg_p = static_cast<uint8_t>((tint_color >> 8) & 0xFF);
        uint8_t tb_p = static_cast<uint8_t>(tint_color & 0xFF);
        uint8_t ta_p = static_cast<uint8_t>((tint_color >> 24) & 0xFF);

        uint32_t s_px = *src++;
        uint8_t sa = div255_scalar(((s_px >> 24) & 0xFF) * ta_p);
        if (sa == 0) {
            dst++;
        } else {
            uint32_t d_px = *dst;
            uint8_t inv_a = static_cast<uint8_t>(255 - sa);
            uint8_t sr = div255_scalar(((s_px >> 16) & 0xFF) * tr_p);
            uint8_t sg = div255_scalar(((s_px >> 8) & 0xFF) * tg_p);
            uint8_t sb = div255_scalar((s_px & 0xFF) * tb_p);

            uint8_t da = static_cast<uint8_t>((d_px >> 24) & 0xFF);
            uint8_t dr = static_cast<uint8_t>((d_px >> 16) & 0xFF);
            uint8_t dg = static_cast<uint8_t>((d_px >> 8) & 0xFF);
            uint8_t db = static_cast<uint8_t>(d_px & 0xFF);

            uint8_t out_r = div255_scalar(sr * sa + dr * inv_a);
            uint8_t out_g = div255_scalar(sg * sa + dg * inv_a);
            uint8_t out_b = div255_scalar(sb * sa + db * inv_a);
            uint8_t out_a = div255_scalar(255 * sa + da * inv_a);

            *dst++ = (static_cast<uint32_t>(out_a) << 24) |
                     (static_cast<uint32_t>(out_r) << 16) |
                     (static_cast<uint32_t>(out_g) << 8)  |
                     static_cast<uint32_t>(out_b);
        }
        count--;
    }

    if (((reinterpret_cast<uintptr_t>(dst) | reinterpret_cast<uintptr_t>(src)) & 7) == 0) {
        uint8x8_t t_b = vdup_n_u8(static_cast<uint8_t>(tint_color & 0xFF));
        uint8x8_t t_g = vdup_n_u8(static_cast<uint8_t>((tint_color >> 8) & 0xFF));
        uint8x8_t t_r = vdup_n_u8(static_cast<uint8_t>((tint_color >> 16) & 0xFF));
        uint8x8_t t_a = vdup_n_u8(static_cast<uint8_t>((tint_color >> 24) & 0xFF));

        auto tint_vec = [](uint8x8_t c, uint8x8_t t) -> uint8x8_t {
            uint16x8_t prod = vmull_u8(c, t);
            uint16x8_t temp = vaddq_u16(prod, vshrq_n_u16(prod, 8));
            temp = vaddq_u16(temp, vdupq_n_u16(1));
            return vshrn_n_u16(temp, 8);
        };

        while (count >= 8) {
            uint8x8x4_t s = vld4_u8(reinterpret_cast<const uint8_t*>(src));
            uint8x8x4_t d = vld4_u8(reinterpret_cast<const uint8_t*>(dst));

            s.val[0] = tint_vec(s.val[0], t_b);
            s.val[1] = tint_vec(s.val[1], t_g);
            s.val[2] = tint_vec(s.val[2], t_r);
            s.val[3] = tint_vec(s.val[3], t_a);

            uint8x8_t inv_a = vsub_u8(vdup_n_u8(255), s.val[3]);
            uint8x8x4_t out;
            for (int k = 0; k < 3; ++k) {
                uint16x8_t prod = vmull_u8(s.val[k], s.val[3]);
                prod = vmlal_u8(prod, d.val[k], inv_a);
                uint16x8_t temp = vaddq_u16(prod, vshrq_n_u16(prod, 8));
                temp = vaddq_u16(temp, vdupq_n_u16(1));
                out.val[k] = vshrn_n_u16(temp, 8);
            }

            uint16x8_t a_prod = vmull_u8(s.val[3], vdup_n_u8(255));
            a_prod = vmlal_u8(a_prod, d.val[3], inv_a);
            uint16x8_t a_temp = vaddq_u16(a_prod, vshrq_n_u16(a_prod, 8));
            a_temp = vaddq_u16(a_temp, vdupq_n_u16(1));
            out.val[3] = vshrn_n_u16(a_temp, 8);

            vst4_u8(reinterpret_cast<uint8_t*>(dst), out);

            src += 8;
            dst += 8;
            count -= 8;
        }
    }
#endif

    uint8_t tr = static_cast<uint8_t>((tint_color >> 16) & 0xFF);
    uint8_t tg = static_cast<uint8_t>((tint_color >> 8) & 0xFF);
    uint8_t tb = static_cast<uint8_t>(tint_color & 0xFF);
    uint8_t ta = static_cast<uint8_t>((tint_color >> 24) & 0xFF);

    while (count > 0) {
        uint32_t s_px = *src++;
        uint8_t sa = div255_scalar(((s_px >> 24) & 0xFF) * ta);
        if (sa == 0) {
            dst++;
            count--;
            continue;
        }

        uint32_t d_px = *dst;
        uint8_t inv_a = static_cast<uint8_t>(255 - sa);
        uint8_t sr = div255_scalar(((s_px >> 16) & 0xFF) * tr);
        uint8_t sg = div255_scalar(((s_px >> 8) & 0xFF) * tg);
        uint8_t sb = div255_scalar((s_px & 0xFF) * tb);

        uint8_t da = static_cast<uint8_t>((d_px >> 24) & 0xFF);
        uint8_t dr = static_cast<uint8_t>((d_px >> 16) & 0xFF);
        uint8_t dg = static_cast<uint8_t>((d_px >> 8) & 0xFF);
        uint8_t db = static_cast<uint8_t>(d_px & 0xFF);

        uint8_t out_r = div255_scalar(sr * sa + dr * inv_a);
        uint8_t out_g = div255_scalar(sg * sa + dg * inv_a);
        uint8_t out_b = div255_scalar(sb * sa + db * inv_a);
        uint8_t out_a = div255_scalar(255 * sa + da * inv_a);

        *dst++ = (static_cast<uint32_t>(out_a) << 24) |
                 (static_cast<uint32_t>(out_r) << 16) |
                 (static_cast<uint32_t>(out_g) << 8)  |
                 static_cast<uint32_t>(out_b);
        count--;
    }
}

void NeonBlit_AdditiveBlend_Tint_ARGB(uint32_t* dst, const uint32_t* src, size_t count, uint32_t tint_color) {
    if (!dst || !src || count == 0) return;
    if (tint_color == 0xFFFFFFFFu) {
        NeonBlit_AdditiveBlend_ARGB(dst, src, count);
        return;
    }

#if PVZ_HAS_NEON
    uint8x8_t t_b = vdup_n_u8(static_cast<uint8_t>(tint_color & 0xFF));
    uint8x8_t t_g = vdup_n_u8(static_cast<uint8_t>((tint_color >> 8) & 0xFF));
    uint8x8_t t_r = vdup_n_u8(static_cast<uint8_t>((tint_color >> 16) & 0xFF));
    uint8x8_t t_a = vdup_n_u8(static_cast<uint8_t>((tint_color >> 24) & 0xFF));

    auto tint_vec = [](uint8x8_t c, uint8x8_t t) -> uint8x8_t {
        uint16x8_t prod = vmull_u8(c, t);
        uint16x8_t temp = vaddq_u16(prod, vshrq_n_u16(prod, 8));
        temp = vaddq_u16(temp, vdupq_n_u16(1));
        return vshrn_n_u16(temp, 8);
    };

    while (count >= 8) {
        uint8x8x4_t s = vld4_u8(reinterpret_cast<const uint8_t*>(src));
        uint8x8x4_t d = vld4_u8(reinterpret_cast<const uint8_t*>(dst));

        s.val[0] = tint_vec(s.val[0], t_b);
        s.val[1] = tint_vec(s.val[1], t_g);
        s.val[2] = tint_vec(s.val[2], t_r);
        s.val[3] = tint_vec(s.val[3], t_a);

        uint8x8x4_t out;
        for (int k = 0; k < 3; ++k) {
            uint16x8_t prod = vmull_u8(s.val[k], s.val[3]);
            uint16x8_t temp = vaddq_u16(prod, vshrq_n_u16(prod, 8));
            temp = vaddq_u16(temp, vdupq_n_u16(1));
            uint8x8_t add_term = vshrn_n_u16(temp, 8);
            out.val[k] = vqadd_u8(d.val[k], add_term);
        }
        out.val[3] = vqadd_u8(d.val[3], s.val[3]);

        vst4_u8(reinterpret_cast<uint8_t*>(dst), out);

        src += 8;
        dst += 8;
        count -= 8;
    }
#endif

    uint8_t tr = static_cast<uint8_t>((tint_color >> 16) & 0xFF);
    uint8_t tg = static_cast<uint8_t>((tint_color >> 8) & 0xFF);
    uint8_t tb = static_cast<uint8_t>(tint_color & 0xFF);
    uint8_t ta = static_cast<uint8_t>((tint_color >> 24) & 0xFF);

    while (count > 0) {
        uint32_t s_px = *src++;
        uint8_t sa = div255_scalar(((s_px >> 24) & 0xFF) * ta);
        if (sa == 0) {
            dst++;
            count--;
            continue;
        }

        uint32_t d_px = *dst;
        uint8_t sr = div255_scalar(((s_px >> 16) & 0xFF) * tr);
        uint8_t sg = div255_scalar(((s_px >> 8) & 0xFF) * tg);
        uint8_t sb = div255_scalar((s_px & 0xFF) * tb);

        uint8_t da = static_cast<uint8_t>((d_px >> 24) & 0xFF);
        uint8_t dr = static_cast<uint8_t>((d_px >> 16) & 0xFF);
        uint8_t dg = static_cast<uint8_t>((d_px >> 8) & 0xFF);
        uint8_t db = static_cast<uint8_t>(d_px & 0xFF);

        uint8_t add_r = div255_scalar(sr * sa);
        uint8_t add_g = div255_scalar(sg * sa);
        uint8_t add_b = div255_scalar(sb * sa);

        uint8_t out_r = static_cast<uint8_t>(std::min(255, dr + add_r));
        uint8_t out_g = static_cast<uint8_t>(std::min(255, dg + add_g));
        uint8_t out_b = static_cast<uint8_t>(std::min(255, db + add_b));
        uint8_t out_a = static_cast<uint8_t>(std::min(255, da + sa));

        *dst++ = (static_cast<uint32_t>(out_a) << 24) |
                 (static_cast<uint32_t>(out_r) << 16) |
                 (static_cast<uint32_t>(out_g) << 8)  |
                 static_cast<uint32_t>(out_b);
        count--;
    }
}

void NeonBlit_RGB565_From_ARGB(uint16_t* dst, const uint32_t* src, size_t count) {
    if (!dst || !src || count == 0) return;

#if PVZ_HAS_NEON
    while (count >= 8) {
        uint8x8x4_t s = vld4_u8(reinterpret_cast<const uint8_t*>(src));
        // s.val[0] = B, s.val[1] = G, s.val[2] = R
        uint16x8_t b = vmovl_u8(s.val[0]);
        uint16x8_t g = vmovl_u8(s.val[1]);
        uint16x8_t r = vmovl_u8(s.val[2]);

        uint16x8_t r5 = vshlq_n_u16(vshrq_n_u16(r, 3), 11);
        uint16x8_t g6 = vshlq_n_u16(vshrq_n_u16(g, 2), 5);
        uint16x8_t b5 = vshrq_n_u16(b, 3);

        uint16x8_t rgb565 = vorrq_u16(vorrq_u16(r5, g6), b5);
        vst1q_u16(dst, rgb565);

        src += 8;
        dst += 8;
        count -= 8;
    }
#endif

    while (count > 0) {
        uint32_t px = *src++;
        uint8_t r = static_cast<uint8_t>((px >> 16) & 0xFF);
        uint8_t g = static_cast<uint8_t>((px >> 8) & 0xFF);
        uint8_t b = static_cast<uint8_t>(px & 0xFF);
        *dst++ = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        count--;
    }
}

void NeonBlit_Span(uint32_t* dst, const uint32_t* src, size_t count, BlitMode mode, uint32_t tint_color) {
    switch (mode) {
        case BLIT_COPY:
            if (tint_color == 0xFFFFFFFFu) {
                std::memcpy(dst, src, count * sizeof(uint32_t));
            } else {
                NeonBlit_AlphaBlend_Tint_ARGB(dst, src, count, tint_color);
            }
            break;
        case BLIT_ALPHA:
            if (tint_color == 0xFFFFFFFFu) {
                NeonBlit_AlphaBlend_ARGB(dst, src, count);
            } else {
                NeonBlit_AlphaBlend_Tint_ARGB(dst, src, count, tint_color);
            }
            break;
        case BLIT_ADDITIVE:
            if (tint_color == 0xFFFFFFFFu) {
                NeonBlit_AdditiveBlend_ARGB(dst, src, count);
            } else {
                NeonBlit_AdditiveBlend_Tint_ARGB(dst, src, count, tint_color);
            }
            break;
    }
}

} // namespace Graphics
} // namespace PvZ
