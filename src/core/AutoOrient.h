#pragma once
#include <vector>

#include "core/ChartSpec.h"
#include "core/PatchSampler.h"
#include "core/Targets.h"

namespace ccm {

// One way of assigning the four clicked points to the chart corners.
struct OrientationCandidate {
    ChartCorners corners;
    int quarterTurns = 0;  // rotateCorners() steps applied after the optional flip
    bool flipped = false;
    double meanDeltaE = 0;  // ΔE2000 of a 3x4 fit on raw DN with this patch order
    int usedPatches = 0;
};

// Tries all 8 corner assignments (4 rotations x mirror) and ranks them by how well a CCM
// fits the chart reference (black level of `settings` is ignored). A wrong patch order cannot be fitted well, so the best candidate
// is almost always the true orientation. Sorted best first; the clicked order itself is the
// candidate with quarterTurns == 0 and flipped == false.
std::vector<OrientationCandidate> rankOrientations(const cv::Mat& rgb, const ChartCorners& clicked,
                                                   const SampleSettings& settings, const ChartSpec& chart,
                                                   const Targets& targets, const std::vector<bool>& excluded);

}  // namespace ccm
