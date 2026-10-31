#pragma once
#include <vector>

#include "core/ChartSpec.h"
#include "core/ColorMath.h"
#include "core/Illuminant.h"

namespace ccm {

struct TargetOptions {
    std::string outputSpace = "srgb";
    // true : output is white balanced (reference adapted D50 -> output white).
    // false: keep the illuminant color cast (D50 -> illuminant white, no adaptation to output).
    bool adaptToOutputWhite = true;
};

struct Targets {
    std::vector<Vec3> linearRgb;  // target linear RGB in the output space
    std::vector<Vec3> lab;        // target CIELAB relative to the output white (for ΔE)
};

Targets computeTargets(const ChartSpec& chart, const Illuminant& illuminant, const TargetOptions& opt);

// CIELAB (relative to the output white) of a linear RGB value in the output space.
Vec3 linearRgbToLab(const Vec3& rgb, const RgbSpace& space);

}  // namespace ccm
