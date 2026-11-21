#include "ReanimRigEvaluator.h"
#include "PvzpLib/Reanimator.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif
#include <cmath>
#include <cstring>

alignas(64) TrackTransform gTransformCache[MAX_ACTIVE_RIGS][MAX_TRACKS_PER_RIG];

namespace ReanimRigEvaluator {

void BeginFrame() {
    // Zero-overhead frame synchronization hook
}

void EvaluateSingleRig(Reanimation* theReanim, int cacheIndex) {
    if (!theReanim || cacheIndex < 0 || cacheIndex >= MAX_ACTIVE_RIGS) return;
    if (!theReanim->mDefinition) return;

    int trackCount = theReanim->mDefinition->mTracks.count;
    if (trackCount > MAX_TRACKS_PER_RIG) trackCount = MAX_TRACKS_PER_RIG;

    for (int t = 0; t < trackCount; ++t) {
        TrackTransform& outT = gTransformCache[cacheIndex][t];
        theReanim->CalculateModelMatrix(t, outT.mMatrix, &outT.mAlpha, &outT.mColor);
    }
}

void EvaluateBatch(Reanimation** theRigs, int startIndex, int count) {
    if (!theRigs || startIndex < 0 || count <= 0) return;

    for (int i = 0; i < count; ++i) {
        int idx = startIndex + i;
        Reanimation* rig = theRigs[idx];
        if (rig && rig->mCacheIndex >= 0 && rig->mCacheIndex < MAX_ACTIVE_RIGS) {
            EvaluateSingleRig(rig, rig->mCacheIndex);
        }
    }
}

} // namespace ReanimRigEvaluator
