#ifndef PVZ_GRAPHICS_SOFTWARERENDERER_HPP
#define PVZ_GRAPHICS_SOFTWARERENDERER_HPP

#include <cstddef>
#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>

#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#elif __has_include(<SDL/SDL.h>)
#include <SDL/SDL.h>
#elif __has_include(<SDL.h>)
#include <SDL.h>
#else
struct SDL_Rect {
    int16_t x, y;
    uint16_t w, h;
};
struct SDL_Color {
    uint8_t r, g, b, a;
};
struct SDL_PixelFormat {
    uint32_t format;
    uint8_t BitsPerPixel;
    uint8_t BytesPerPixel;
    uint32_t Rmask, Gmask, Bmask, Amask;
};
struct SDL_Surface {
    uint32_t flags;
    SDL_PixelFormat *format;
    int w, h;
    int pitch;
    void *pixels;
    void *userdata;
    int locked;
    void *lock_data;
    SDL_Rect clip_rect;
    void *map;
    int refcount;
};
#endif

#include "graphics/NeonBlit.hpp"

namespace PvZ {
namespace Graphics {

// ---------------------------------------------------------------------------
// 2D Geometry & Color Types
// ---------------------------------------------------------------------------

struct Point2D {
    float x = 0.0f;
    float y = 0.0f;

    Point2D() = default;
    Point2D(float _x, float _y) : x(_x), y(_y) {}
};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    Rect() = default;
    Rect(int _x, int _y, int _w, int _h) : x(_x), y(_y), w(_w), h(_h) {}

    bool IsEmpty() const { return w <= 0 || h <= 0; }
    bool Contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    bool Intersects(const Rect& o) const {
        return !(x + w <= o.x || o.x + o.w <= x || y + h <= o.y || o.y + o.h <= y);
    }
    Rect Intersection(const Rect& o) const {
        int x1 = std::max(x, o.x);
        int y1 = std::max(y, o.y);
        int x2 = std::min(x + w, o.x + o.w);
        int y2 = std::min(y + h, o.y + o.h);
        if (x2 <= x1 || y2 <= y1) return Rect(0, 0, 0, 0);
        return Rect(x1, y1, x2 - x1, y2 - y1);
    }
};

struct FloatRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    FloatRect() = default;
    FloatRect(float _x, float _y, float _w, float _h) : x(_x), y(_y), w(_w), h(_h) {}
};

struct Color {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;

    Color() = default;
    Color(uint8_t _r, uint8_t _g, uint8_t _b, uint8_t _a = 255)
        : r(_r), g(_g), b(_b), a(_a) {}
    explicit Color(uint32_t argb)
        : r(static_cast<uint8_t>((argb >> 16) & 0xFF)),
          g(static_cast<uint8_t>((argb >> 8) & 0xFF)),
          b(static_cast<uint8_t>(argb & 0xFF)),
          a(static_cast<uint8_t>((argb >> 24) & 0xFF)) {}

    uint32_t ToARGB() const {
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8)  |
               static_cast<uint32_t>(b);
    }

    static Color FromARGB(uint32_t argb) { return Color(argb); }
};

// ---------------------------------------------------------------------------
// 3x3 Affine Matrix for Reanim / Camera Transformations
// ---------------------------------------------------------------------------

struct Matrix3 {
    float m[3][3];

    Matrix3() {
        IdentityInternal();
    }

    void IdentityInternal() {
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                m[r][c] = (r == c) ? 1.0f : 0.0f;
            }
        }
    }

    static Matrix3 Identity() {
        return Matrix3();
    }

    static Matrix3 MakeTranslation(float tx, float ty) {
        Matrix3 res;
        res.m[0][2] = tx;
        res.m[1][2] = ty;
        return res;
    }

    static Matrix3 MakeScale(float sx, float sy) {
        Matrix3 res;
        res.m[0][0] = sx;
        res.m[1][1] = sy;
        return res;
    }

    static Matrix3 MakeRotation(float radians) {
        Matrix3 res;
        float c = std::cos(radians);
        float s = std::sin(radians);
        res.m[0][0] = c;
        res.m[0][1] = -s;
        res.m[1][0] = s;
        res.m[1][1] = c;
        return res;
    }

    Matrix3 operator*(const Matrix3& rhs) const {
        Matrix3 out;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                out.m[r][c] = m[r][0] * rhs.m[0][c] +
                              m[r][1] * rhs.m[1][c] +
                              m[r][2] * rhs.m[2][c];
            }
        }
        return out;
    }

    Point2D Transform(float x, float y) const {
        return Point2D(
            m[0][0] * x + m[0][1] * y + m[0][2],
            m[1][0] * x + m[1][1] * y + m[1][2]
        );
    }

    Point2D Transform(const Point2D& p) const {
        return Transform(p.x, p.y);
    }
};

// ---------------------------------------------------------------------------
// Vertex structure for Textured Quads & Triangles (Reanim / Particles)
// ---------------------------------------------------------------------------

struct Vertex2D {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    Color color;

    Vertex2D() = default;
    Vertex2D(float _x, float _y, float _u, float _v, Color _c = Color(255, 255, 255, 255))
        : x(_x), y(_y), u(_u), v(_v), color(_c) {}
};

// ---------------------------------------------------------------------------
// Pre-allocated DMA Texture Pool (SDL_MMIYOO_TEXTURE_POOL=1)
// ---------------------------------------------------------------------------

class DmaTexturePool {
public:
    static constexpr size_t DEFAULT_POOL_SIZE = 16 * 1024 * 1024; // 16 MB

    static DmaTexturePool& Instance();

    bool Initialize(size_t pool_size = DEFAULT_POOL_SIZE);
    void Shutdown();
    void* Allocate(size_t size, size_t alignment = 64);
    void Reset();

    bool IsActive() const { return mActive; }
    size_t GetTotalMemory() const { return mTotalSize; }
    size_t GetUsedMemory() const { return mUsedOffset; }
    size_t GetAvailableMemory() const { return mTotalSize > mUsedOffset ? mTotalSize - mUsedOffset : 0; }

private:
    DmaTexturePool() = default;
    ~DmaTexturePool();

    uint8_t* mPoolBuffer = nullptr;
    size_t mTotalSize = 0;
    size_t mUsedOffset = 0;
    bool mActive = false;
};

// ---------------------------------------------------------------------------
// Software Surface (wraps SDL_Surface or DMA buffer)
// ---------------------------------------------------------------------------

class SoftwareSurface {
public:
    int mWidth = 0;
    int mHeight = 0;
    int mPitch = 0;
    int mBytesPerPixel = 4; // 4 for ARGB8888, 2 for RGB565
    uint8_t* mPixels = nullptr;
    bool mOwnsPixels = false;
    bool mFromPool = false;
    SDL_Surface* mSdlSurface = nullptr;

    SoftwareSurface() = default;
    SoftwareSurface(int w, int h, int bpp = 32, bool use_pool = false);
    explicit SoftwareSurface(SDL_Surface* sdl_surface);
    SoftwareSurface(void* pixels, int w, int h, int pitch, int bpp = 32);
    ~SoftwareSurface();

    void Clear(uint32_t color);
    uint32_t GetPixel(int x, int y) const;
    void SetPixel(int x, int y, uint32_t color);
};

// ---------------------------------------------------------------------------
// Global Viewport Projection Pipeline (800x600 -> 640x480 at 0.8x)
// ---------------------------------------------------------------------------

class ViewportProjection {
public:
    static constexpr int LOGICAL_WIDTH = 800;
    static constexpr int LOGICAL_HEIGHT = 600;
    static constexpr int PHYSICAL_WIDTH = 640;
    static constexpr int PHYSICAL_HEIGHT = 480;
    static constexpr float DEFAULT_SCALE_X = 0.8f;
    static constexpr float DEFAULT_SCALE_Y = 0.8f;

    ViewportProjection(int logical_w = LOGICAL_WIDTH, int logical_h = LOGICAL_HEIGHT,
                       int phys_w = PHYSICAL_WIDTH, int phys_h = PHYSICAL_HEIGHT);

    Point2D LogicalToPhysical(float lx, float ly) const;
    Point2D PhysicalToLogical(float px, float py) const;
    Rect LogicalToPhysical(const Rect& logical_rect) const;
    FloatRect LogicalToPhysical(const FloatRect& logical_rect) const;

    Matrix3 GetProjectionMatrix() const;
    Matrix3 ApplyTo(const Matrix3& local_matrix) const;

    Rect ClipToPhysicalBounds(const Rect& phys_rect) const;
    bool IsRectVisible(const Rect& phys_rect) const;

    float GetScaleX() const { return mScaleX; }
    float GetScaleY() const { return mScaleY; }
    int GetPhysicalWidth() const { return mPhysW; }
    int GetPhysicalHeight() const { return mPhysH; }
    int GetLogicalWidth() const { return mLogicalW; }
    int GetLogicalHeight() const { return mLogicalH; }

private:
    int mLogicalW = LOGICAL_WIDTH;
    int mLogicalH = LOGICAL_HEIGHT;
    int mPhysW = PHYSICAL_WIDTH;
    int mPhysH = PHYSICAL_HEIGHT;
    float mScaleX = DEFAULT_SCALE_X;
    float mScaleY = DEFAULT_SCALE_Y;
};

// ---------------------------------------------------------------------------
// Software Renderer Engine Backend
// ---------------------------------------------------------------------------

class SoftwareRenderer {
public:
    enum DrawMode {
        DRAWMODE_NORMAL = 0,
        DRAWMODE_ADDITIVE = 1
    };

    SoftwareRenderer();
    ~SoftwareRenderer();

    bool Initialize(int phys_w = 640, int phys_h = 480, SDL_Surface* target_surface = nullptr);
    void Shutdown();

    // Target surface / framebuffer binding
    void SetTarget(SoftwareSurface* target);
    void SetTarget(SDL_Surface* sdl_target);
    SoftwareSurface* GetTarget() const { return mCurrentTarget; }

    // Viewport projection access
    const ViewportProjection& GetViewport() const { return mViewport; }
    void SetViewport(const ViewportProjection& vp);

    // SexyApp Graphics State
    void SetColor(const Color& color) { mCurrentColor = color; }
    Color GetColor() const { return mCurrentColor; }
    void SetDrawMode(int draw_mode) { mDrawMode = draw_mode; }
    int GetDrawMode() const { return mDrawMode; }
    void SetColorizeImages(bool colorize) { mColorizeImages = colorize; }
    bool GetColorizeImages() const { return mColorizeImages; }

    // Clipping Rectangles (input in 800x600 logical coordinates)
    void SetClipRect(int x, int y, int w, int h);
    void SetClipRect(const Rect& rect);
    void ClearClipRect();
    void ClipRect(int x, int y, int w, int h);
    void PushClipRect(const Rect& rect);
    void PopClipRect();
    Rect GetClipRect() const { return mLogicalClipRect; }
    Rect GetPhysicalClipRect() const { return mPhysClipRect; }

    // Transform stack for Reanim and camera
    void PushTransform(const Matrix3& m);
    void PopTransform();
    void SetTransform(const Matrix3& m);
    Matrix3 GetCurrentTransform() const;

    // Primitives in logical 800x600 coordinates
    void Clear(const Color& color);
    void DrawPoint(int x, int y);
    void DrawLine(int start_x, int start_y, int end_x, int end_y);
    void DrawRect(int x, int y, int w, int h);
    void FillRect(int x, int y, int w, int h);

    // Sprite and image blitting
    void DrawImage(SoftwareSurface* image, int x, int y);
    void DrawImage(SoftwareSurface* image, int x, int y, const Rect& src_rect);
    void DrawImage(SoftwareSurface* image, const Rect& dst_rect, const Rect& src_rect);
    void DrawImageColor(SoftwareSurface* image, int x, int y, const Color& tint);

    // Reanim & particle quads
    void DrawQuad(SoftwareSurface* texture,
                  const Vertex2D& v0, const Vertex2D& v1,
                  const Vertex2D& v2, const Vertex2D& v3);
    void DrawTriangleTex(SoftwareSurface* texture,
                         const Vertex2D& v0, const Vertex2D& v1, const Vertex2D& v2);

    // Presentation / Flip
    void Present();

private:
    void UpdatePhysicalClipRect();
    void RasterizeTriangleSpan(SoftwareSurface* texture,
                               float x0, float y0, float u0, float v0,
                               float x1, float y1, float u1, float v1,
                               float x2, float y2, float u2, float v2,
                               uint32_t tint_color);

    ViewportProjection mViewport;
    SoftwareSurface* mCurrentTarget = nullptr;
    SoftwareSurface* mOwnedTarget = nullptr;

    Color mCurrentColor = Color(255, 255, 255, 255);
    int mDrawMode = DRAWMODE_NORMAL;
    bool mColorizeImages = false;

    Rect mLogicalClipRect;
    Rect mPhysClipRect;
    std::vector<Rect> mClipStack;
    std::vector<Matrix3> mTransformStack;
};

} // namespace Graphics
} // namespace PvZ

#endif // PVZ_GRAPHICS_SOFTWARERENDERER_HPP
