#pragma once
#include <opencv2/core.hpp>
#include <string>

namespace ccm {

struct LoadedImage {
    cv::Mat rgb;          // CV_8UC3 or CV_16UC3, R,G,B channel order
    int containerBits = 8;  // 8 or 16
    std::string path;
};

enum class RawLayout { RGB, BGR, BayerRGGB, BayerBGGR, BayerGRBG, BayerGBRG, Mono };

struct RawParams {
    int width = 0;
    int height = 0;
    int bytesPerSample = 1;  // 1 or 2 (little endian)
    int headerBytes = 0;
    RawLayout layout = RawLayout::BayerRGGB;
};

bool loadImageFile(const std::string& path, LoadedImage& out, std::string& error);
bool loadRawFile(const std::string& path, const RawParams& params, LoadedImage& out, std::string& error);

// Downscale so that the longer side is at most maxDim (area interpolation), keeping depth.
cv::Mat downscale(const cv::Mat& img, int maxDim);

}  // namespace ccm
