#pragma once

#include <cstdint>

constexpr int MAX_ACTIVE_RIGS = 256;
constexpr int MAX_TRACKS_PER_RIG = 48;

struct alignas(32) TrackTransform {
    float    mMatrix[2][3]; // 24 bytes (2D affine: [m00, m01, m02; m10, m11, m12])
    float    mAlpha;        // 4 bytes
    uint32_t mColor;        // 4 bytes
}; // Exactly 32 bytes

static_assert(sizeof(TrackTransform) == 32, "TrackTransform must be exactly 32 bytes");

alignas(64) extern TrackTransform gTransformCache[MAX_ACTIVE_RIGS][MAX_TRACKS_PER_RIG];

class Reanimation;

namespace ReanimRigEvaluator {
    void BeginFrame();
    void EvaluateBatch(Reanimation** theRigs, int startIndex, int count);
    void EvaluateSingleRig(Reanimation* theReanim, int cacheIndex);
}
