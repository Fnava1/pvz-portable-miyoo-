#include "graphics/SoftwareRenderer.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>

namespace PvZ {
namespace Graphics {

// ---------------------------------------------------------------------------
// DmaTexturePool Implementation
// ---------------------------------------------------------------------------

DmaTexturePool& DmaTexturePool::Instance() {
    static DmaTexturePool s_instance;
    return s_instance;
}

DmaTexturePool::~DmaTexturePool() {
    Shutdown();
}

bool DmaTexturePool::Initialize(size_t pool_size) {
    if (mActive && mPoolBuffer) {
        return true;
    }

    mTotalSize = pool_size;
    mUsedOffset = 0;

#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 64, mTotalSize) == 0) {
        mPoolBuffer = static_cast<uint8_t*>(ptr);
    }
#else
    mPoolBuffer = static_cast<uint8_t*>(std::malloc(mTotalSize));
#endif

    if (!mPoolBuffer) {
        mActive = false;
        return false;
    }

    std::memset(mPoolBuffer, 0, mTotalSize);
    mActive = true;
    return true;
}

void DmaTexturePool::Shutdown() {
    if (mPoolBuffer) {
        std::free(mPoolBuffer);
        mPoolBuffer = nullptr;
    }
    mTotalSize = 0;
    mUsedOffset = 0;
    mActive = false;
}

void* DmaTexturePool::Allocate(size_t size, size_t alignment) {
    if (!mActive || !mPoolBuffer) {
        return nullptr;
    }

    size_t aligned_offset = (mUsedOffset + alignment - 1) & ~(alignment - 1);
    if (aligned_offset + size > mTotalSize) {
        return nullptr; // Out of pool capacity
    }

    void* result = mPoolBuffer + aligned_offset;
    mUsedOffset = aligned_offset + size;
    return result;
}

void DmaTexturePool::Reset() {
    mUsedOffset = 0;
}

// ---------------------------------------------------------------------------
// SoftwareSurface Implementation
// ---------------------------------------------------------------------------

SoftwareSurface::SoftwareSurface(int w, int h, int bpp, bool use_pool)
    : mWidth(w), mHeight(h), mBytesPerPixel(bpp / 8) {
    mPitch = mWidth * mBytesPerPixel;
    size_t total_bytes = static_cast<size_t>(mPitch) * mHeight;

    if (use_pool && DmaTexturePool::Instance().IsActive()) {
        mPixels = static_cast<uint8_t*>(DmaTexturePool::Instance().Allocate(total_bytes, 64));
        if (mPixels) {
            mFromPool = true;
            mOwnsPixels = false;
        }
    }

    if (!mPixels) {
        mPixels = new uint8_t[total_bytes]();
        mOwnsPixels = true;
        mFromPool = false;
    }
}

SoftwareSurface::SoftwareSurface(SDL_Surface* sdl_surface)
    : mSdlSurface(sdl_surface) {
    if (mSdlSurface) {
        mWidth = mSdlSurface->w;
        mHeight = mSdlSurface->h;
        mPitch = mSdlSurface->pitch;
        mBytesPerPixel = mSdlSurface->format ? mSdlSurface->format->BytesPerPixel : 4;
        mPixels = static_cast<uint8_t*>(mSdlSurface->pixels);
        mOwnsPixels = false;
        mFromPool = false;
    }
}

SoftwareSurface::SoftwareSurface(void* pixels, int w, int h, int pitch, int bpp)
    : mWidth(w), mHeight(h), mPitch(pitch), mBytesPerPixel(bpp / 8),
      mPixels(static_cast<uint8_t*>(pixels)), mOwnsPixels(false), mFromPool(false) {}

SoftwareSurface::~SoftwareSurface() {
    if (mOwnsPixels && mPixels) {
        delete[] mPixels;
        mPixels = nullptr;
    }
}

void SoftwareSurface::Clear(uint32_t color) {
    if (!mPixels || mWidth <= 0 || mHeight <= 0) return;

    if (mBytesPerPixel == 4) {
        for (int y = 0; y < mHeight; ++y) {
            uint32_t* row = reinterpret_cast<uint32_t*>(mPixels + y * mPitch);
            NeonBlit_Fill_ARGB(row, mWidth, color);
        }
    } else if (mBytesPerPixel == 2) {
        uint8_t r = static_cast<uint8_t>((color >> 16) & 0xFF);
        uint8_t g = static_cast<uint8_t>((color >> 8) & 0xFF);
        uint8_t b = static_cast<uint8_t>(color & 0xFF);
        uint16_t c16 = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        for (int y = 0; y < mHeight; ++y) {
            uint16_t* row = reinterpret_cast<uint16_t*>(mPixels + y * mPitch);
            std::fill(row, row + mWidth, c16);
        }
    }
}

uint32_t SoftwareSurface::GetPixel(int x, int y) const {
    if (!mPixels || x < 0 || x >= mWidth || y < 0 || y >= mHeight) return 0;
    if (mBytesPerPixel == 4) {
        const uint32_t* row = reinterpret_cast<const uint32_t*>(mPixels + y * mPitch);
        return row[x];
    } else if (mBytesPerPixel == 2) {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(mPixels + y * mPitch);
        uint16_t c16 = row[x];
        uint32_t r = ((c16 >> 11) & 0x1F) << 3;
        uint32_t g = ((c16 >> 5) & 0x3F) << 2;
        uint32_t b = (c16 & 0x1F) << 3;
        return (0xFF000000u) | (r << 16) | (g << 8) | b;
    }
    return 0;
}

void SoftwareSurface::SetPixel(int x, int y, uint32_t color) {
    if (!mPixels || x < 0 || x >= mWidth || y < 0 || y >= mHeight) return;
    if (mBytesPerPixel == 4) {
        uint32_t* row = reinterpret_cast<uint32_t*>(mPixels + y * mPitch);
        row[x] = color;
    } else if (mBytesPerPixel == 2) {
        uint16_t* row = reinterpret_cast<uint16_t*>(mPixels + y * mPitch);
        uint8_t r = static_cast<uint8_t>((color >> 16) & 0xFF);
        uint8_t g = static_cast<uint8_t>((color >> 8) & 0xFF);
        uint8_t b = static_cast<uint8_t>(color & 0xFF);
        row[x] = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
}

// ---------------------------------------------------------------------------
// ViewportProjection Implementation
// ---------------------------------------------------------------------------

ViewportProjection::ViewportProjection(int logical_w, int logical_h, int phys_w, int phys_h)
    : mLogicalW(logical_w), mLogicalH(logical_h), mPhysW(phys_w), mPhysH(phys_h) {
    mScaleX = static_cast<float>(mPhysW) / static_cast<float>(mLogicalW);
    mScaleY = static_cast<float>(mPhysH) / static_cast<float>(mLogicalH);
}

Point2D ViewportProjection::LogicalToPhysical(float lx, float ly) const {
    return Point2D(lx * mScaleX, ly * mScaleY);
}

Point2D ViewportProjection::PhysicalToLogical(float px, float py) const {
    return Point2D(px / mScaleX, py / mScaleY);
}

Rect ViewportProjection::LogicalToPhysical(const Rect& logical_rect) const {
    int px = static_cast<int>(std::round(logical_rect.x * mScaleX));
    int py = static_cast<int>(std::round(logical_rect.y * mScaleY));
    int pw = static_cast<int>(std::round(logical_rect.w * mScaleX));
    int ph = static_cast<int>(std::round(logical_rect.h * mScaleY));
    return Rect(px, py, pw, ph);
}

FloatRect ViewportProjection::LogicalToPhysical(const FloatRect& logical_rect) const {
    return FloatRect(
        logical_rect.x * mScaleX,
        logical_rect.y * mScaleY,
        logical_rect.w * mScaleX,
        logical_rect.h * mScaleY
    );
}

Matrix3 ViewportProjection::GetProjectionMatrix() const {
    return Matrix3::MakeScale(mScaleX, mScaleY);
}

Matrix3 ViewportProjection::ApplyTo(const Matrix3& local_matrix) const {
    return GetProjectionMatrix() * local_matrix;
}

Rect ViewportProjection::ClipToPhysicalBounds(const Rect& phys_rect) const {
    Rect screen(0, 0, mPhysW, mPhysH);
    return phys_rect.Intersection(screen);
}

bool ViewportProjection::IsRectVisible(const Rect& phys_rect) const {
    return !ClipToPhysicalBounds(phys_rect).IsEmpty();
}

// ---------------------------------------------------------------------------
// SoftwareRenderer Implementation
// ---------------------------------------------------------------------------

SoftwareRenderer::SoftwareRenderer()
    : mViewport(800, 600, 640, 480),
      mLogicalClipRect(0, 0, 800, 600),
      mPhysClipRect(0, 0, 640, 480) {
    mTransformStack.push_back(Matrix3::Identity());
}

SoftwareRenderer::~SoftwareRenderer() {
    Shutdown();
}

bool SoftwareRenderer::Initialize(int phys_w, int phys_h, SDL_Surface* target_surface) {
    const char* env_pool = std::getenv("SDL_MMIYOO_TEXTURE_POOL");
    if (env_pool && (std::strcmp(env_pool, "1") == 0 || std::strcmp(env_pool, "true") == 0)) {
        DmaTexturePool::Instance().Initialize(16 * 1024 * 1024);
    }

    mViewport = ViewportProjection(800, 600, phys_w, phys_h);
    mLogicalClipRect = Rect(0, 0, 800, 600);
    UpdatePhysicalClipRect();

    if (target_surface) {
        SetTarget(target_surface);
    }
    return true;
}

void SoftwareRenderer::Shutdown() {
    if (mOwnedTarget) {
        delete mOwnedTarget;
        mOwnedTarget = nullptr;
    }
    mCurrentTarget = nullptr;
    mClipStack.clear();
    mTransformStack.clear();
    mTransformStack.push_back(Matrix3::Identity());
}

void SoftwareRenderer::SetTarget(SoftwareSurface* target) {
    if (mOwnedTarget) {
        delete mOwnedTarget;
        mOwnedTarget = nullptr;
    }
    mCurrentTarget = target;
    if (mCurrentTarget) {
        mViewport = ViewportProjection(800, 600, mCurrentTarget->mWidth, mCurrentTarget->mHeight);
        UpdatePhysicalClipRect();
    }
}

void SoftwareRenderer::SetTarget(SDL_Surface* sdl_target) {
    if (mOwnedTarget) {
        delete mOwnedTarget;
        mOwnedTarget = nullptr;
    }
    if (sdl_target) {
        mOwnedTarget = new SoftwareSurface(sdl_target);
        mCurrentTarget = mOwnedTarget;
        mViewport = ViewportProjection(800, 600, sdl_target->w, sdl_target->h);
        UpdatePhysicalClipRect();
    } else {
        mCurrentTarget = nullptr;
    }
}

void SoftwareRenderer::SetViewport(const ViewportProjection& vp) {
    mViewport = vp;
    UpdatePhysicalClipRect();
}

void SoftwareRenderer::SetClipRect(int x, int y, int w, int h) {
    mLogicalClipRect = Rect(x, y, w, h);
    UpdatePhysicalClipRect();
}

void SoftwareRenderer::SetClipRect(const Rect& rect) {
    mLogicalClipRect = rect;
    UpdatePhysicalClipRect();
}

void SoftwareRenderer::ClearClipRect() {
    mLogicalClipRect = Rect(0, 0, mViewport.GetLogicalWidth(), mViewport.GetLogicalHeight());
    UpdatePhysicalClipRect();
}

void SoftwareRenderer::ClipRect(int x, int y, int w, int h) {
    Rect next = mLogicalClipRect.Intersection(Rect(x, y, w, h));
    mLogicalClipRect = next;
    UpdatePhysicalClipRect();
}

void SoftwareRenderer::PushClipRect(const Rect& rect) {
    mClipStack.push_back(mLogicalClipRect);
    ClipRect(rect.x, rect.y, rect.w, rect.h);
}

void SoftwareRenderer::PopClipRect() {
    if (!mClipStack.empty()) {
        mLogicalClipRect = mClipStack.back();
        mClipStack.pop_back();
        UpdatePhysicalClipRect();
    }
}

void SoftwareRenderer::UpdatePhysicalClipRect() {
    Rect phys = mViewport.LogicalToPhysical(mLogicalClipRect);
    mPhysClipRect = mViewport.ClipToPhysicalBounds(phys);
}

void SoftwareRenderer::PushTransform(const Matrix3& m) {
    Matrix3 top = mTransformStack.empty() ? Matrix3::Identity() : mTransformStack.back();
    mTransformStack.push_back(top * m);
}

void SoftwareRenderer::PopTransform() {
    if (mTransformStack.size() > 1) {
        mTransformStack.pop_back();
    }
}

void SoftwareRenderer::SetTransform(const Matrix3& m) {
    if (!mTransformStack.empty()) {
        mTransformStack.back() = m;
    } else {
        mTransformStack.push_back(m);
    }
}

Matrix3 SoftwareRenderer::GetCurrentTransform() const {
    return mTransformStack.empty() ? Matrix3::Identity() : mTransformStack.back();
}

void SoftwareRenderer::Clear(const Color& color) {
    if (!mCurrentTarget || mCurrentTarget->mBytesPerPixel != 4) return;

    if (mPhysClipRect.x == 0 && mPhysClipRect.y == 0 &&
        mPhysClipRect.w == mCurrentTarget->mWidth &&
        mPhysClipRect.h == mCurrentTarget->mHeight) {
        mCurrentTarget->Clear(color.ToARGB());
        return;
    }

    FillRect(mLogicalClipRect.x, mLogicalClipRect.y, mLogicalClipRect.w, mLogicalClipRect.h);
}

void SoftwareRenderer::DrawPoint(int x, int y) {
    if (!mCurrentTarget || mCurrentTarget->mBytesPerPixel != 4 || mPhysClipRect.IsEmpty()) return;

    Matrix3 composite = mViewport.ApplyTo(GetCurrentTransform());
    Point2D pt = composite.Transform(static_cast<float>(x), static_cast<float>(y));
    int px = static_cast<int>(std::round(pt.x));
    int py = static_cast<int>(std::round(pt.y));

    if (mPhysClipRect.Contains(px, py)) {
        uint32_t c = mCurrentColor.ToARGB();
        if (mDrawMode == DRAWMODE_ADDITIVE) {
            uint32_t curr = mCurrentTarget->GetPixel(px, py);
            NeonBlit_AdditiveBlend_ARGB(&curr, &c, 1);
            mCurrentTarget->SetPixel(px, py, curr);
        } else if (mCurrentColor.a == 255) {
            mCurrentTarget->SetPixel(px, py, c);
        } else {
            uint32_t curr = mCurrentTarget->GetPixel(px, py);
            NeonBlit_AlphaBlend_ARGB(&curr, &c, 1);
            mCurrentTarget->SetPixel(px, py, curr);
        }
    }
}

void SoftwareRenderer::DrawLine(int start_x, int start_y, int end_x, int end_y) {
    if (!mCurrentTarget || mCurrentTarget->mBytesPerPixel != 4 || mPhysClipRect.IsEmpty()) return;

    Matrix3 composite = mViewport.ApplyTo(GetCurrentTransform());
    Point2D p0 = composite.Transform(static_cast<float>(start_x), static_cast<float>(start_y));
    Point2D p1 = composite.Transform(static_cast<float>(end_x), static_cast<float>(end_y));

    int x0 = static_cast<int>(std::round(p0.x));
    int y0 = static_cast<int>(std::round(p0.y));
    int x1 = static_cast<int>(std::round(p1.x));
    int y1 = static_cast<int>(std::round(p1.y));

    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    uint32_t c = mCurrentColor.ToARGB();

    while (true) {
        if (mPhysClipRect.Contains(x0, y0)) {
            if (mDrawMode == DRAWMODE_ADDITIVE) {
                uint32_t curr = mCurrentTarget->GetPixel(x0, y0);
                NeonBlit_AdditiveBlend_ARGB(&curr, &c, 1);
                mCurrentTarget->SetPixel(x0, y0, curr);
            } else if (mCurrentColor.a == 255) {
                mCurrentTarget->SetPixel(x0, y0, c);
            } else {
                uint32_t curr = mCurrentTarget->GetPixel(x0, y0);
                NeonBlit_AlphaBlend_ARGB(&curr, &c, 1);
                mCurrentTarget->SetPixel(x0, y0, curr);
            }
        }

        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void SoftwareRenderer::DrawRect(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    DrawLine(x, y, x + w - 1, y);
    DrawLine(x + w - 1, y, x + w - 1, y + h - 1);
    DrawLine(x + w - 1, y + h - 1, x, y + h - 1);
    DrawLine(x, y + h - 1, x, y);
}

void SoftwareRenderer::FillRect(int x, int y, int w, int h) {
    if (!mCurrentTarget || mCurrentTarget->mBytesPerPixel != 4 || w <= 0 || h <= 0 || mPhysClipRect.IsEmpty()) return;

    Matrix3 composite = mViewport.ApplyTo(GetCurrentTransform());
    Point2D tl = composite.Transform(static_cast<float>(x), static_cast<float>(y));
    Point2D br = composite.Transform(static_cast<float>(x + w), static_cast<float>(y + h));

    int px = static_cast<int>(std::round(std::min(tl.x, br.x)));
    int py = static_cast<int>(std::round(std::min(tl.y, br.y)));
    int pw = static_cast<int>(std::round(std::abs(br.x - tl.x)));
    int ph = static_cast<int>(std::round(std::abs(br.y - tl.y)));

    Rect phys(px, py, pw, ph);
    Rect clipped = phys.Intersection(mPhysClipRect);
    if (clipped.IsEmpty()) return;

    uint32_t c = mCurrentColor.ToARGB();
    BlitMode mode = (mDrawMode == DRAWMODE_ADDITIVE) ? BLIT_ADDITIVE :
                    (mCurrentColor.a == 255) ? BLIT_COPY : BLIT_ALPHA;

    alignas(8) uint32_t span_buf[640];
    int span_len = std::min(clipped.w, 640);
    if (mode != BLIT_COPY) {
        for (int i = 0; i < span_len; ++i) {
            span_buf[i] = c;
        }
    }

    for (int j = clipped.y; j < clipped.y + clipped.h; ++j) {
        uint32_t* dst_row = reinterpret_cast<uint32_t*>(
            mCurrentTarget->mPixels + j * mCurrentTarget->mPitch
        ) + clipped.x;

        if (mode == BLIT_COPY) {
            NeonBlit_Fill_ARGB(dst_row, span_len, c);
        } else if (mode == BLIT_ALPHA) {
            NeonBlit_AlphaBlend_ARGB(dst_row, span_buf, span_len);
        } else {
            NeonBlit_AdditiveBlend_ARGB(dst_row, span_buf, span_len);
        }
    }
}

void SoftwareRenderer::DrawImage(SoftwareSurface* image, int x, int y) {
    if (!image) return;
    DrawImage(image, x, y, Rect(0, 0, image->mWidth, image->mHeight));
}

void SoftwareRenderer::DrawImage(SoftwareSurface* image, int x, int y, const Rect& src_rect) {
    if (!image) return;
    // Logical destination size is unscaled source dimensions / 0.8
    // When downscaled 80% textures from main_mmp.pak are rendered,
    // their physical size matches src_rect.w and src_rect.h exactly.
    int lw = static_cast<int>(std::round(src_rect.w / mViewport.GetScaleX()));
    int lh = static_cast<int>(std::round(src_rect.h / mViewport.GetScaleY()));
    DrawImage(image, Rect(x, y, lw, lh), src_rect);
}

void SoftwareRenderer::DrawImage(SoftwareSurface* image, const Rect& dst_rect, const Rect& src_rect) {
    if (!mCurrentTarget || !image || mCurrentTarget->mBytesPerPixel != 4 || image->mBytesPerPixel != 4 ||
        dst_rect.IsEmpty() || src_rect.IsEmpty() || mPhysClipRect.IsEmpty()) return;

    Matrix3 composite = mViewport.ApplyTo(GetCurrentTransform());
    Point2D tl = composite.Transform(static_cast<float>(dst_rect.x), static_cast<float>(dst_rect.y));
    Point2D br = composite.Transform(static_cast<float>(dst_rect.x + dst_rect.w),
                                     static_cast<float>(dst_rect.y + dst_rect.h));

    int px = static_cast<int>(std::round(std::min(tl.x, br.x)));
    int py = static_cast<int>(std::round(std::min(tl.y, br.y)));
    int pw = static_cast<int>(std::round(std::abs(br.x - tl.x)));
    int ph = static_cast<int>(std::round(std::abs(br.y - tl.y)));

    Rect phys(px, py, pw, ph);
    Rect clipped = phys.Intersection(mPhysClipRect);
    if (clipped.IsEmpty()) return;

    uint32_t tint = mColorizeImages ? mCurrentColor.ToARGB() : 0xFFFFFFFFu;
    BlitMode mode = (mDrawMode == DRAWMODE_ADDITIVE) ? BLIT_ADDITIVE : BLIT_ALPHA;

    // Fast Path: 1:1 physical pixel match (optimized 80% assets from main_mmp.pak)
    if (pw == src_rect.w && ph == src_rect.h) {
        int src_offset_x = src_rect.x + (clipped.x - phys.x);
        int src_offset_y = src_rect.y + (clipped.y - phys.y);

        for (int row = 0; row < clipped.h; ++row) {
            int sy = src_offset_y + row;
            int dy = clipped.y + row;
            if (sy < 0 || sy >= image->mHeight || dy < 0 || dy >= mCurrentTarget->mHeight) continue;

            const uint32_t* src_ptr = reinterpret_cast<const uint32_t*>(
                image->mPixels + sy * image->mPitch
            ) + src_offset_x;

            uint32_t* dst_ptr = reinterpret_cast<uint32_t*>(
                mCurrentTarget->mPixels + dy * mCurrentTarget->mPitch
            ) + clipped.x;

            NeonBlit_Span(dst_ptr, src_ptr, clipped.w, mode, tint);
        }
        return;
    }

    // Scaling Path: Bilinear/nearest sampling with stack buffer (zero heap allocation)
    alignas(8) uint32_t span_buf[640];
    int span_len = std::min(clipped.w, 640);
    float u_scale = static_cast<float>(src_rect.w) / static_cast<float>(pw);
    float v_scale = static_cast<float>(src_rect.h) / static_cast<float>(ph);

    for (int row = 0; row < clipped.h; ++row) {
        int dy = clipped.y + row;
        float v_sample = src_rect.y + (clipped.y + row - phys.y) * v_scale;
        int sy = std::clamp(static_cast<int>(v_sample), 0, image->mHeight - 1);
        const uint32_t* src_row = reinterpret_cast<const uint32_t*>(image->mPixels + sy * image->mPitch);

        for (int col = 0; col < span_len; ++col) {
            float u_sample = src_rect.x + (clipped.x + col - phys.x) * u_scale;
            int sx = std::clamp(static_cast<int>(u_sample), 0, image->mWidth - 1);
            span_buf[col] = src_row[sx];
        }

        uint32_t* dst_ptr = reinterpret_cast<uint32_t*>(
            mCurrentTarget->mPixels + dy * mCurrentTarget->mPitch
        ) + clipped.x;

        NeonBlit_Span(dst_ptr, span_buf, span_len, mode, tint);
    }
}

void SoftwareRenderer::DrawImageColor(SoftwareSurface* image, int x, int y, const Color& tint) {
    Color old_c = mCurrentColor;
    bool old_col = mColorizeImages;
    mCurrentColor = tint;
    mColorizeImages = true;
    DrawImage(image, x, y);
    mCurrentColor = old_c;
    mColorizeImages = old_col;
}

void SoftwareRenderer::DrawQuad(SoftwareSurface* texture,
                                const Vertex2D& v0, const Vertex2D& v1,
                                const Vertex2D& v2, const Vertex2D& v3) {
    if (!mCurrentTarget || !texture || mCurrentTarget->mBytesPerPixel != 4 || texture->mBytesPerPixel != 4 ||
        mPhysClipRect.IsEmpty()) return;

    Matrix3 composite = mViewport.ApplyTo(GetCurrentTransform());
    Point2D p0 = composite.Transform(v0.x, v0.y);
    Point2D p1 = composite.Transform(v1.x, v1.y);
    Point2D p2 = composite.Transform(v2.x, v2.y);
    Point2D p3 = composite.Transform(v3.x, v3.y);

    // Bounding-box early rejection
    float min_x = std::min({p0.x, p1.x, p2.x, p3.x});
    float max_x = std::max({p0.x, p1.x, p2.x, p3.x});
    float min_y = std::min({p0.y, p1.y, p2.y, p3.y});
    float max_y = std::max({p0.y, p1.y, p2.y, p3.y});

    if (max_x < mPhysClipRect.x || min_x >= mPhysClipRect.x + mPhysClipRect.w ||
        max_y < mPhysClipRect.y || min_y >= mPhysClipRect.y + mPhysClipRect.h) {
        return; // Culled
    }

    uint32_t tint = mColorizeImages ? mCurrentColor.ToARGB() : v0.color.ToARGB();

    // Split quad into two triangles
    RasterizeTriangleSpan(texture, p0.x, p0.y, v0.u, v0.v,
                          p1.x, p1.y, v1.u, v1.v,
                          p2.x, p2.y, v2.u, v2.v, tint);

    RasterizeTriangleSpan(texture, p0.x, p0.y, v0.u, v0.v,
                          p2.x, p2.y, v2.u, v2.v,
                          p3.x, p3.y, v3.u, v3.v, tint);
}

void SoftwareRenderer::DrawTriangleTex(SoftwareSurface* texture,
                                       const Vertex2D& v0, const Vertex2D& v1, const Vertex2D& v2) {
    if (!mCurrentTarget || !texture || mCurrentTarget->mBytesPerPixel != 4 || texture->mBytesPerPixel != 4 ||
        mPhysClipRect.IsEmpty()) return;

    Matrix3 composite = mViewport.ApplyTo(GetCurrentTransform());
    Point2D p0 = composite.Transform(v0.x, v0.y);
    Point2D p1 = composite.Transform(v1.x, v1.y);
    Point2D p2 = composite.Transform(v2.x, v2.y);

    float min_x = std::min({p0.x, p1.x, p2.x});
    float max_x = std::max({p0.x, p1.x, p2.x});
    float min_y = std::min({p0.y, p1.y, p2.y});
    float max_y = std::max({p0.y, p1.y, p2.y});

    if (max_x < mPhysClipRect.x || min_x >= mPhysClipRect.x + mPhysClipRect.w ||
        max_y < mPhysClipRect.y || min_y >= mPhysClipRect.y + mPhysClipRect.h) {
        return; // Culled
    }

    uint32_t tint = mColorizeImages ? mCurrentColor.ToARGB() : v0.color.ToARGB();
    RasterizeTriangleSpan(texture, p0.x, p0.y, v0.u, v0.v,
                          p1.x, p1.y, v1.u, v1.v,
                          p2.x, p2.y, v2.u, v2.v, tint);
}

void SoftwareRenderer::RasterizeTriangleSpan(SoftwareSurface* texture,
                                            float x0, float y0, float u0, float v0,
                                            float x1, float y1, float u1, float v1,
                                            float x2, float y2, float u2, float v2,
                                            uint32_t tint_color) {
    if (!mCurrentTarget || !texture || mCurrentTarget->mBytesPerPixel != 4 || texture->mBytesPerPixel != 4 ||
        mPhysClipRect.IsEmpty()) return;

    // Sort vertices by Y: y0 <= y1 <= y2
    if (y1 < y0) {
        std::swap(x0, x1); std::swap(y0, y1);
        std::swap(u0, u1); std::swap(v0, v1);
    }
    if (y2 < y0) {
        std::swap(x0, x2); std::swap(y0, y2);
        std::swap(u0, u2); std::swap(v0, v2);
    }
    if (y2 < y1) {
        std::swap(x1, x2); std::swap(y1, y2);
        std::swap(u1, u2); std::swap(v1, v2);
    }

    int start_y = std::max(static_cast<int>(std::ceil(y0)), mPhysClipRect.y);
    int end_y = std::min(static_cast<int>(std::floor(y2)), mPhysClipRect.y + mPhysClipRect.h - 1);
    if (start_y > end_y) return;

    float total_height = y2 - y0;
    if (std::abs(total_height) < 1e-4f) return;

    BlitMode mode = (mDrawMode == DRAWMODE_ADDITIVE) ? BLIT_ADDITIVE : BLIT_ALPHA;

    alignas(8) uint32_t span_pixels[640];

    for (int y = start_y; y <= end_y; ++y) {
        bool second_half = y > y1 || std::abs(y1 - y0) < 1e-4f;
        float segment_height = second_half ? (y2 - y1) : (y1 - y0);
        if (std::abs(segment_height) < 1e-4f) continue;

        float alpha = (static_cast<float>(y) - y0) / total_height;
        float beta = (static_cast<float>(y) - (second_half ? y1 : y0)) / segment_height;

        float xa = x0 + (x2 - x0) * alpha;
        float ua = u0 + (u2 - u0) * alpha;
        float va = v0 + (v2 - v0) * alpha;

        float xb = second_half ? (x1 + (x2 - x1) * beta) : (x0 + (x1 - x0) * beta);
        float ub = second_half ? (u1 + (u2 - u1) * beta) : (u0 + (u1 - u0) * beta);
        float vb = second_half ? (v1 + (v2 - v1) * beta) : (v0 + (v1 - v0) * beta);

        if (xa > xb) {
            std::swap(xa, xb);
            std::swap(ua, ub);
            std::swap(va, vb);
        }

        int scan_x0 = std::max(static_cast<int>(std::ceil(xa)), mPhysClipRect.x);
        int scan_x1 = std::min(static_cast<int>(std::floor(xb)), mPhysClipRect.x + mPhysClipRect.w - 1);
        if (scan_x0 > scan_x1) continue;

        int span_len = scan_x1 - scan_x0 + 1;
        if (span_len > 640) span_len = 640;

        float span_w = (xb - xa > 1e-4f) ? (xb - xa) : 1.0f;

        for (int x = 0; x < span_len; ++x) {
            float t = static_cast<float>(scan_x0 + x - xa) / span_w;
            float u = ua + (ub - ua) * t;
            float v = va + (vb - va) * t;

            // UV sampling without discontinuity on edge overshoot
            int tex_x = (u > 1.5f) ?
                        std::clamp(static_cast<int>(u), 0, texture->mWidth - 1) :
                        std::clamp(static_cast<int>(u * texture->mWidth), 0, texture->mWidth - 1);
            int tex_y = (v > 1.5f) ?
                        std::clamp(static_cast<int>(v), 0, texture->mHeight - 1) :
                        std::clamp(static_cast<int>(v * texture->mHeight), 0, texture->mHeight - 1);

            span_pixels[x] = texture->GetPixel(tex_x, tex_y);
        }

        uint32_t* dst_ptr = reinterpret_cast<uint32_t*>(
            mCurrentTarget->mPixels + y * mCurrentTarget->mPitch
        ) + scan_x0;

        NeonBlit_Span(dst_ptr, span_pixels, span_len, mode, tint_color);
    }
}

void SoftwareRenderer::Present() {
    // For direct framebuffer surfaces, flushes display lines or triggers SDL_Flip / pan
    if (mCurrentTarget && mCurrentTarget->mSdlSurface) {
#if defined(SDL_FLIP) || defined(SDL_DOUBLEBUF)
        // If SDL 1.2 / 2 surface
#endif
    }
}

} // namespace Graphics
} // namespace PvZ
