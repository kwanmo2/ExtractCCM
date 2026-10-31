#pragma once
#include <string>
#include <vector>

#include "core/PatchSampler.h"
#include "core/Targets.h"

namespace ccm {

// Chart-free calibration: the user picks the same surface point in a reference image
// (e.g. a phone photo, sRGB-encoded) and in the camera image (raw, linear).
struct PointPair {
    std::string name;
    cv::Point2f ref;   // reference image pixel
    cv::Point2f cam;   // camera image pixel
    bool neutral = false;  // gray/white surface: used to neutralize the reference and for white balance
    bool enabled = true;
};

enum class RefEncoding { SRGB, Gamma22, Linear };

struct ReferenceSample {
    cv::Vec3d linear;  // trimmed mean of the decoded (linear) pixels
    cv::Vec3d stdDev;  // per-pixel, linear
    double saturatedRatio = 0.0;
    int pixelCount = 0;
};

// Decodes every pixel before averaging, so the mean is a true linear mean.
ReferenceSample sampleReference(const cv::Mat& rgb, const Quad& quad, RefEncoding encoding,
                                double saturationFraction = 0.98, double trimFraction = 0.10);

struct PointTargetOptions {
    std::string outputSpace = "srgb";
    bool neutralizeReference = true;  // make the neutral points gray (removes the reference's color cast)
    bool matchBrightness = false;     // false: keep only the reference chromaticity, brightness from the camera
};

struct PointTargets {
    bool ok = false;
    std::string message;
    Targets targets;
    cv::Vec3d referenceGains{1, 1, 1};  // applied to the reference by the neutralization
};

// refLinear: reference samples (linear sRGB); cam: camera samples (normalized, black subtracted).
PointTargets buildPointTargets(const std::vector<cv::Vec3d>& refLinear, const std::vector<cv::Vec3d>& cam,
                               const std::vector<bool>& neutral, const std::vector<bool>& included,
                               const PointTargetOptions& opt);

}  // namespace ccm
