#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include "../src/SexyAppFramework/misc/SexyMatrix.h"

using namespace Sexy;

static bool float_near(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

static void test_identity_multiplication() {
    std::cout << "[TEST] Running test_identity_multiplication..." << std::endl;
    SexyMatrix3 id;
    id.LoadIdentity();

    SexyTransform2D mat;
    mat.Translate(45.0f, 60.0f);
    mat.Scale(1.5f, 2.0f);
    mat.RotateRad(0.5f);

    SexyMatrix3 res = mat * id;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            assert(float_near(res.m[r][c], mat.m[r][c]));
        }
    }

    SexyMatrix3 res2 = id * mat;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            assert(float_near(res2.m[r][c], mat.m[r][c]));
        }
    }
}

static void test_translation_composition() {
    std::cout << "[TEST] Running test_translation_composition..." << std::endl;
    SexyTransform2D t1;
    t1.Translate(100.0f, 200.0f);

    SexyTransform2D t2;
    t2.Translate(50.0f, -30.0f);

    SexyMatrix3 combined = t1 * t2;
    assert(float_near(combined.m02, 150.0f));
    assert(float_near(combined.m12, 170.0f));
    assert(float_near(combined.m20, 0.0f));
    assert(float_near(combined.m21, 0.0f));
    assert(float_near(combined.m22, 1.0f));

    SexyVector2 pt(10.0f, 20.0f);
    SexyVector2 transformed = combined * pt;
    assert(float_near(transformed.x, 160.0f));
    assert(float_near(transformed.y, 190.0f));
}

static void test_scale_rotation_composition() {
    std::cout << "[TEST] Running test_scale_rotation_composition..." << std::endl;
    SexyTransform2D s;
    s.Scale(2.0f, 3.0f);

    SexyTransform2D r;
    r.RotateRad(static_cast<float>(M_PI / 2.0)); // 90 degrees

    SexyMatrix3 sr = s * r;
    SexyVector2 pt(1.0f, 0.0f);
    SexyVector2 res = sr * pt;
    // (1, 0) rotated 90 deg = (0, 1), scaled by (2, 3) = (0, 3)
    assert(float_near(res.x, 0.0f));
    assert(float_near(res.y, 3.0f));
}

int main() {
    test_identity_multiplication();
    test_translation_composition();
    test_scale_rotation_composition();
    std::cout << "[PASS] All 2D affine matrix tests passed successfully!" << std::endl;
    return 0;
}
