#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include "graphics/NeonBlit.hpp"
#include "graphics/SoftwareRenderer.hpp"

using namespace PvZ::Graphics;

static bool float_near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

// ---------------------------------------------------------------------------
// Step 1: Test 0.8x quad coordinate mapping (400, 300, 100, 100) -> (320, 240, 80, 80)
// ---------------------------------------------------------------------------
static void test_quad_coordinate_mapping() {
    std::cout << "[TEST] Running test_quad_coordinate_mapping (400, 300, 100, 100)..." << std::endl;

    ViewportProjection vp(800, 600, 640, 480);
    assert(float_near(vp.GetScaleX(), 0.8f));
    assert(float_near(vp.GetScaleY(), 0.8f));

    // Quad coordinate in logical space (800x600)
    Rect logical_quad(400, 300, 100, 100);
    Rect phys_quad = vp.LogicalToPhysical(logical_quad);

    assert(phys_quad.x == 320);
    assert(phys_quad.y == 240);
    assert(phys_quad.w == 80);
    assert(phys_quad.h == 80);

    // Also test floating-point rect mapping
    FloatRect logical_fquad(400.0f, 300.0f, 100.0f, 100.0f);
    FloatRect phys_fquad = vp.LogicalToPhysical(logical_fquad);
    assert(float_near(phys_fquad.x, 320.0f));
    assert(float_near(phys_fquad.y, 240.0f));
    assert(float_near(phys_fquad.w, 80.0f));
    assert(float_near(phys_fquad.h, 80.0f));

    // Test individual 4 vertices of the quad
    Point2D v0 = vp.LogicalToPhysical(400.0f, 300.0f); // Top-Left
    Point2D v1 = vp.LogicalToPhysical(500.0f, 300.0f); // Top-Right
    Point2D v2 = vp.LogicalToPhysical(500.0f, 400.0f); // Bottom-Right
    Point2D v3 = vp.LogicalToPhysical(400.0f, 400.0f); // Bottom-Left

    assert(float_near(v0.x, 320.0f) && float_near(v0.y, 240.0f));
    assert(float_near(v1.x, 400.0f) && float_near(v1.y, 240.0f));
    assert(float_near(v2.x, 400.0f) && float_near(v2.y, 320.0f));
    assert(float_near(v3.x, 320.0f) && float_near(v3.y, 320.0f));

    std::cout << "  -> PASS: Logical (400, 300, 100, 100) exactly maps to Physical (320, 240, 80, 80)" << std::endl;
}

// ---------------------------------------------------------------------------
// Step 2: Test point transformations and inverse mapping
// ---------------------------------------------------------------------------
static void test_point_transformations() {
    std::cout << "[TEST] Running test_point_transformations..." << std::endl;

    ViewportProjection vp;

    // Origin (0,0) -> (0,0)
    Point2D p0 = vp.LogicalToPhysical(0.0f, 0.0f);
    assert(float_near(p0.x, 0.0f) && float_near(p0.y, 0.0f));

    // Bottom-right corner (800, 600) -> (640, 480)
    Point2D p_corner = vp.LogicalToPhysical(800.0f, 600.0f);
    assert(float_near(p_corner.x, 640.0f) && float_near(p_corner.y, 480.0f));

    // Center point (400, 300) -> (320, 240)
    Point2D p_center = vp.LogicalToPhysical(400.0f, 300.0f);
    assert(float_near(p_center.x, 320.0f) && float_near(p_center.y, 240.0f));

    // Arbitrary point (150, 250) -> (120, 200)
    Point2D p_arb = vp.LogicalToPhysical(150.0f, 250.0f);
    assert(float_near(p_arb.x, 120.0f) && float_near(p_arb.y, 200.0f));

    // Inverse mapping (Physical to Logical)
    Point2D p_inv = vp.PhysicalToLogical(320.0f, 240.0f);
    assert(float_near(p_inv.x, 400.0f) && float_near(p_inv.y, 300.0f));

    Point2D p_inv_corner = vp.PhysicalToLogical(640.0f, 480.0f);
    assert(float_near(p_inv_corner.x, 800.0f) && float_near(p_inv_corner.y, 600.0f));

    std::cout << "  -> PASS: Forward and inverse point transformations verified" << std::endl;
}

// ---------------------------------------------------------------------------
// Step 3: Test bounding rectangles, visibility, and screen boundary clipping
// ---------------------------------------------------------------------------
static void test_bounding_rectangles_and_clipping() {
    std::cout << "[TEST] Running test_bounding_rectangles_and_clipping..." << std::endl;

    ViewportProjection vp;

    // Full screen rect
    Rect full_logical(0, 0, 800, 600);
    Rect full_phys = vp.LogicalToPhysical(full_logical);
    assert(full_phys.x == 0 && full_phys.y == 0 && full_phys.w == 640 && full_phys.h == 480);
    assert(vp.IsRectVisible(full_phys));

    // Rect completely outside to the right/bottom
    Rect offscreen_rb(850, 650, 100, 100);
    Rect phys_rb = vp.LogicalToPhysical(offscreen_rb);
    assert(!vp.IsRectVisible(phys_rb));
    Rect clipped_rb = vp.ClipToPhysicalBounds(phys_rb);
    assert(clipped_rb.IsEmpty());

    // Rect completely outside to the left/top (negative coords)
    Rect offscreen_lt(-150, -100, 100, 80);
    Rect phys_lt = vp.LogicalToPhysical(offscreen_lt);
    assert(!vp.IsRectVisible(phys_lt));
    Rect clipped_lt = vp.ClipToPhysicalBounds(phys_lt);
    assert(clipped_lt.IsEmpty());

    // Rect straddling screen right edge: (750, 100, 100, 100) -> phys (600, 80, 80, 80)
    // Physical bounds are [0, 640) x [0, 480).
    // Width should be clipped from 80 down to 40 (600 + 40 = 640).
    Rect straddle_right(750, 100, 100, 100);
    Rect phys_straddle = vp.LogicalToPhysical(straddle_right);
    assert(phys_straddle.x == 600 && phys_straddle.y == 80 && phys_straddle.w == 80 && phys_straddle.h == 80);
    assert(vp.IsRectVisible(phys_straddle));
    Rect clipped_straddle = vp.ClipToPhysicalBounds(phys_straddle);
    assert(clipped_straddle.x == 600);
    assert(clipped_straddle.y == 80);
    assert(clipped_straddle.w == 40); // 640 - 600
    assert(clipped_straddle.h == 80);

    // Rect straddling top-left corner: (-50, -50, 150, 150) -> phys (-40, -40, 120, 120)
    Rect straddle_tl(-50, -50, 150, 150);
    Rect phys_tl = vp.LogicalToPhysical(straddle_tl);
    assert(vp.IsRectVisible(phys_tl));
    Rect clipped_tl = vp.ClipToPhysicalBounds(phys_tl);
    assert(clipped_tl.x == 0);
    assert(clipped_tl.y == 0);
    assert(clipped_tl.w == 80); // 120 - 40
    assert(clipped_tl.h == 80); // 120 - 40

    std::cout << "  -> PASS: Bounding rectangle visibility and screen boundary clipping verified" << std::endl;
}

// ---------------------------------------------------------------------------
// Step 4: Test Affine Matrix Multiplication (Reanim hierarchical transforms)
// ---------------------------------------------------------------------------
static void test_affine_matrix_multiplication() {
    std::cout << "[TEST] Running test_affine_matrix_multiplication..." << std::endl;

    ViewportProjection vp;
    Matrix3 proj = vp.GetProjectionMatrix();

    // Verify projection matrix has 0.8x on diagonal
    assert(float_near(proj.m[0][0], 0.8f));
    assert(float_near(proj.m[1][1], 0.8f));
    assert(float_near(proj.m[2][2], 1.0f));
    assert(float_near(proj.m[0][1], 0.0f));
    assert(float_near(proj.m[1][0], 0.0f));

    // Test Point through projection matrix
    Point2D p = proj.Transform(400.0f, 300.0f);
    assert(float_near(p.x, 320.0f) && float_near(p.y, 240.0f));

    // Test local translation matrix (e.g. bone offset in Reanim)
    Matrix3 bone_trans = Matrix3::MakeTranslation(100.0f, 50.0f);
    Matrix3 composite = vp.ApplyTo(bone_trans); // proj * bone_trans

    // Origin (0,0) with bone translation (100, 50) projected by 0.8x -> (80, 40)
    Point2D bone_pt = composite.Transform(0.0f, 0.0f);
    assert(float_near(bone_pt.x, 80.0f));
    assert(float_near(bone_pt.y, 40.0f));

    // Test Reanim bone hierarchy: Parent arm bone + Child hand bone
    Matrix3 arm_trans = Matrix3::MakeTranslation(200.0f, 150.0f);
    Matrix3 hand_offset = Matrix3::MakeTranslation(50.0f, 30.0f);
    Matrix3 hierarchy = arm_trans * hand_offset; // Local composite (250, 180)

    Matrix3 final_transform = vp.ApplyTo(hierarchy);
    Point2D hand_pt = final_transform.Transform(0.0f, 0.0f);
    assert(float_near(hand_pt.x, 250.0f * 0.8f)); // 200.0
    assert(float_near(hand_pt.y, 180.0f * 0.8f)); // 144.0

    // Test 90-degree rotation matrix combined with scale
    Matrix3 rot90 = Matrix3::MakeRotation(1.57079632679f); // 90 degrees
    Matrix3 scaled_rot = vp.ApplyTo(rot90);
    // Vector (100, 0) rotated by 90 deg -> (0, 100), then scaled 0.8x -> (0, 80)
    Point2D rot_pt = scaled_rot.Transform(100.0f, 0.0f);
    assert(float_near(rot_pt.x, 0.0f, 0.01f));
    assert(float_near(rot_pt.y, 80.0f, 0.01f));

    std::cout << "  -> PASS: Affine matrix composition matches Reanim bone hierarchy under 0.8x projection" << std::endl;
}

// ---------------------------------------------------------------------------
// Step 5: Test NEON and scalar alpha / additive blend implementations
// ---------------------------------------------------------------------------
static void test_neon_and_scalar_blitting() {
    std::cout << "[TEST] Running test_neon_and_scalar_blitting..." << std::endl;

    std::cout << "  Hardware NEON available: " << (IsNeonSupported() ? "YES" : "NO (Scalar Fallback)") << std::endl;

    // Test 1: Alpha blend 8 pixels
    // Source: Semi-transparent red (A=128, R=255, G=0, B=0) -> 0x80FF0000
    // Dest:   Opaque blue (A=255, R=0, G=0, B=255)         -> 0xFF0000FF
    // Blended:
    // R = (255 * 128 + 0 * 127) / 255 = ~128
    // G = 0
    // B = (0 * 128 + 255 * 127) / 255 = ~127
    // A = 255
    std::vector<uint32_t> src(13, 0x80FF0000u); // 13 pixels to test 8-vector + 5-remainder
    std::vector<uint32_t> dst(13, 0xFF0000FFu);

    NeonBlit_AlphaBlend_ARGB(dst.data(), src.data(), dst.size());

    for (size_t i = 0; i < dst.size(); ++i) {
        uint32_t pixel = dst[i];
        uint8_t a = (pixel >> 24) & 0xFF;
        uint8_t r = (pixel >> 16) & 0xFF;
        uint8_t g = (pixel >> 8) & 0xFF;
        uint8_t b = pixel & 0xFF;

        assert(a == 255);
        assert(r >= 127 && r <= 129); // Tolerance around 128
        assert(g == 0);
        assert(b >= 126 && b <= 128); // Tolerance around 127
    }

    // Test 2: Additive blend 8 pixels (PvZ particles, sun, projectiles)
    // Dest: Red 0xFF800000 (R=128)
    // Src:  Green 0xFF008000 (G=128, A=255)
    // Expected: R=128, G=128, B=0, A=255 -> 0xFF808000
    std::vector<uint32_t> add_src(16, 0xFF008000u);
    std::vector<uint32_t> add_dst(16, 0xFF800000u);

    NeonBlit_AdditiveBlend_ARGB(add_dst.data(), add_src.data(), add_dst.size());
    for (size_t i = 0; i < add_dst.size(); ++i) {
        uint32_t p = add_dst[i];
        uint8_t r = (p >> 16) & 0xFF;
        uint8_t g = (p >> 8) & 0xFF;
        uint8_t b = p & 0xFF;
        assert(r == 128);
        assert(g == 128);
        assert(b == 0);
    }

    // Test 3: Additive blend saturation / clamping
    // Dest: 0xFFF00000 (R=240)
    // Src:  0xFF800000 (R=128)
    // Expected R: clamp(240 + 128) = 255
    std::vector<uint32_t> sat_src(8, 0xFF800000u);
    std::vector<uint32_t> sat_dst(8, 0xFFF00000u);
    NeonBlit_AdditiveBlend_ARGB(sat_dst.data(), sat_src.data(), sat_dst.size());
    for (size_t i = 0; i < sat_dst.size(); ++i) {
        uint8_t r = (sat_dst[i] >> 16) & 0xFF;
        assert(r == 255);
    }

    std::cout << "  -> PASS: Alpha blend and additive saturation verified across 8-pixel SIMD vectors" << std::endl;
}

// ---------------------------------------------------------------------------
// Step 6: Test DMA Texture Pool (16 MB allocation & alignment)
// ---------------------------------------------------------------------------
static void test_dma_texture_pool() {
    std::cout << "[TEST] Running test_dma_texture_pool..." << std::endl;

    DmaTexturePool& pool = DmaTexturePool::Instance();
    bool ok = pool.Initialize(16 * 1024 * 1024); // 16 MB
    assert(ok);
    assert(pool.IsActive());
    assert(pool.GetTotalMemory() == 16 * 1024 * 1024);
    assert(pool.GetUsedMemory() == 0);

    // Allocate 640x480x4 framebuffer (1.2 MB) with 64-byte alignment
    size_t fb_size = 640 * 480 * 4;
    void* ptr1 = pool.Allocate(fb_size, 64);
    assert(ptr1 != nullptr);
    assert((reinterpret_cast<uintptr_t>(ptr1) % 64) == 0);
    assert(pool.GetUsedMemory() >= fb_size);

    // Allocate a second texture block
    size_t tex_size = 256 * 256 * 4;
    void* ptr2 = pool.Allocate(tex_size, 64);
    assert(ptr2 != nullptr);
    assert((reinterpret_cast<uintptr_t>(ptr2) % 64) == 0);
    assert(ptr2 > ptr1);

    // Reset pool
    pool.Reset();
    assert(pool.GetUsedMemory() == 0);

    std::cout << "  -> PASS: DMA Texture Pool successfully manages 16 MB with 64-byte cache alignment" << std::endl;
}

// ---------------------------------------------------------------------------
// Step 7: Test SoftwareRenderer end-to-end draw calls with 0.8x viewport projection
// ---------------------------------------------------------------------------
static void test_software_renderer_e2e() {
    std::cout << "[TEST] Running test_software_renderer_e2e..." << std::endl;

    SoftwareSurface fb(640, 480, 32);
    fb.Clear(0xFF000000u); // Clear black

    SoftwareRenderer renderer;
    renderer.Initialize(640, 480);
    renderer.SetTarget(&fb);

    // 1. Draw a filled rectangle in logical space at (400, 300, 100, 100) with solid Red
    renderer.SetColor(Color(255, 0, 0, 255));
    renderer.FillRect(400, 300, 100, 100);

    // In physical framebuffer:
    // (400, 300, 100, 100) * 0.8 = (320, 240, 80, 80)
    // Pixel (320, 240) MUST be RED (0xFFFF0000)
    // Pixel (399, 319) MUST be RED (bottom-right inside)
    // Pixel (319, 239) MUST be BLACK (outside left-top)
    // Pixel (400, 320) MUST be BLACK (outside right-bottom)
    uint32_t* pixels = reinterpret_cast<uint32_t*>(fb.mPixels);

    uint32_t inside_tl = pixels[240 * 640 + 320];
    uint32_t inside_br = pixels[319 * 640 + 399];
    uint32_t outside_lt = pixels[239 * 640 + 319];
    uint32_t outside_rb = pixels[320 * 640 + 400];

    assert(inside_tl == 0xFFFF0000u);
    assert(inside_br == 0xFFFF0000u);
    assert(outside_lt == 0xFF000000u);
    assert(outside_rb == 0xFF000000u);

    // 2. Test clipping rectangle in renderer
    // Set logical clip rect to (400, 300, 50, 50) -> phys (320, 240, 40, 40)
    // Paint Green across (400, 300, 100, 100)
    renderer.SetClipRect(400, 300, 50, 50);
    renderer.SetColor(Color(0, 255, 0, 255));
    renderer.FillRect(400, 300, 100, 100);

    // Inside clip (e.g. 320, 240) should now be Green (0xFF00FF00)
    // Outside clip but inside original rect (e.g. 370, 290) should remain Red (0xFFFF0000)
    uint32_t in_clip = pixels[240 * 640 + 320];
    uint32_t out_clip = pixels[290 * 640 + 370];

    assert(in_clip == 0xFF00FF00u);
    assert(out_clip == 0xFFFF0000u);

    // 3. Test textured quad blitting with 0.8x projection
    SoftwareSurface tex(100, 100, 32);
    tex.Clear(0xFF0000FFu); // Blue texture (100x100)

    renderer.ClearClipRect();
    Vertex2D v0 = {0.0f, 0.0f, 0.0f, 0.0f, Color(255, 255, 255, 255)};
    Vertex2D v1 = {100.0f, 0.0f, 1.0f, 0.0f, Color(255, 255, 255, 255)};
    Vertex2D v2 = {100.0f, 100.0f, 1.0f, 1.0f, Color(255, 255, 255, 255)};
    Vertex2D v3 = {0.0f, 100.0f, 0.0f, 1.0f, Color(255, 255, 255, 255)};

    renderer.DrawQuad(&tex, v0, v1, v2, v3);
    // (0, 0, 100, 100) * 0.8 = (0, 0, 80, 80)
    // Pixel (40, 40) should be Blue (0xFF0000FF)
    // Pixel (81, 81) should be Black (0xFF000000)
    uint32_t quad_inside = pixels[40 * 640 + 40];
    uint32_t quad_outside = pixels[81 * 640 + 81];
    assert(quad_inside == 0xFF0000FFu);
    assert(quad_outside == 0xFF000000u);

    std::cout << "  -> PASS: End-to-end software renderer renders accurately onto 640x480 surface" << std::endl;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::cout << "==================================================" << std::endl;
    std::cout << " Running PvZ Viewport Scaling & Software Rasterizer Tests" << std::endl;
    std::cout << "==================================================" << std::endl;

    test_quad_coordinate_mapping();
    test_point_transformations();
    test_bounding_rectangles_and_clipping();
    test_affine_matrix_multiplication();
    test_neon_and_scalar_blitting();
    test_dma_texture_pool();
    test_software_renderer_e2e();

    std::cout << "==================================================" << std::endl;
    std::cout << " ALL VIEWPORT SCALING & RASTERIZER TESTS PASSED!" << std::endl;
    std::cout << "==================================================" << std::endl;
    return 0;
}
