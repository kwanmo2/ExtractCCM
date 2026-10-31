#pragma once
#include <array>
#include <opencv2/core.hpp>
#include <vector>

namespace ccm {

using Quad = std::array<cv::Point2f, 4>;

// Corner convention: centers of the patches at (row 0, col 0), (row 0, last col),
// (last row, last col), (last row, col 0) in the chart's reference orientation.
using ChartCorners = std::array<cv::Point2f, 4>;

struct SampleSettings {
    int rows = 4;
    int cols = 6;
    double sampleFraction = 0.5;  // side of the sampled square relative to the patch pitch
    cv::Vec3d black{0, 0, 0};     // black level per channel (DN)
    double fullScale = 255.0;     // white clip level (DN)
    double saturationFraction = 0.98;
    double trimFraction = 0.10;   // trimmed from each end before averaging
};

struct PatchStat {
    Quad quad;
    int pixelCount = 0;
    cv::Vec3d meanDN;          // trimmed mean, raw DN (no black subtraction)
    cv::Vec3d mean;            // (meanDN - black) / (fullScale - black)
    cv::Vec3d stdDev;          // per-pixel, normalized units
    double saturatedRatio = 0.0;
    cv::Vec2d chroma;          // opponent chromaticity of the mean (see chromaOf)
    double chromaNoise = 0.0;  // per-pixel std of the chromaticity
};

// Opponent chromaticity (R-G, (R+G)/2-B) / (R+G+B); 0 for non-positive sums.
cv::Vec2d chromaOf(const cv::Vec3d& rgb);

cv::Matx33d gridToImage(const ChartCorners& corners, int rows, int cols);
std::vector<Quad> patchQuads(const ChartCorners& corners, int rows, int cols, double fraction);
std::vector<PatchStat> samplePatches(const cv::Mat& rgb, const ChartCorners& corners, const SampleSettings& s);
PatchStat sampleQuad(const cv::Mat& rgb, const Quad& quad, const SampleSettings& s);  // rows/cols unused
Quad squareAround(cv::Point2f center, double halfSize);

// Raw channel values of the pixels inside a convex quad (8- or 16-bit RGB image).
void quadPixels(const cv::Mat& rgb, const Quad& quad, std::vector<double> ch[3]);
double trimmedMean(std::vector<double> v, double trimFraction);  // trims trimFraction from each end

// Rotates the corner assignment by 90° steps, for charts photographed rotated.
ChartCorners rotateCorners(const ChartCorners& c, int quarterTurns);
ChartCorners flipCorners(const ChartCorners& c);

}  // namespace ccm
