#include "core/ImageIO.h"

#include <fstream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace ccm {

namespace {

bool finish(cv::Mat img, const std::string& path, LoadedImage& out, std::string& error) {
    if (img.depth() != CV_8U && img.depth() != CV_16U) {
        error = "Only 8-bit and 16-bit images are supported.";
        return false;
    }
    out.rgb = img;
    out.containerBits = img.depth() == CV_8U ? 8 : 16;
    out.path = path;
    return true;
}

}  // namespace

bool loadImageFile(const std::string& path, LoadedImage& out, std::string& error) {
    cv::Mat img = cv::imread(path, cv::IMREAD_UNCHANGED | cv::IMREAD_ANYDEPTH | cv::IMREAD_ANYCOLOR);
    if (img.empty()) {
        error = "Cannot read image: " + path;
        return false;
    }
    cv::Mat rgb;
    switch (img.channels()) {
        case 1:
            error = "Single-channel image: use 'Open RAW' with a Bayer layout to demosaic it.";
            return false;
        case 3: cv::cvtColor(img, rgb, cv::COLOR_BGR2RGB); break;
        case 4: cv::cvtColor(img, rgb, cv::COLOR_BGRA2RGB); break;
        default:
            error = "Unsupported channel count.";
            return false;
    }
    return finish(rgb, path, out, error);
}

bool loadRawFile(const std::string& path, const RawParams& p, LoadedImage& out, std::string& error) {
    if (p.width <= 0 || p.height <= 0 || (p.bytesPerSample != 1 && p.bytesPerSample != 2)) {
        error = "Invalid RAW parameters.";
        return false;
    }
    const bool interleaved = p.layout == RawLayout::RGB || p.layout == RawLayout::BGR;
    const int channels = interleaved ? 3 : 1;
    const size_t bytes = size_t(p.width) * p.height * channels * p.bytesPerSample;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Cannot open " + path;
        return false;
    }
    in.seekg(0, std::ios::end);
    const size_t fileSize = size_t(in.tellg());
    if (fileSize < p.headerBytes + bytes) {
        error = "File is smaller than header + width*height*channels*bytes (" + std::to_string(fileSize) +
                " < " + std::to_string(p.headerBytes + bytes) + ").";
        return false;
    }
    in.seekg(p.headerBytes);
    const int type = CV_MAKETYPE(p.bytesPerSample == 1 ? CV_8U : CV_16U, channels);
    cv::Mat raw(p.height, p.width, type);
    in.read(reinterpret_cast<char*>(raw.data), std::streamsize(bytes));

    cv::Mat rgb;
    switch (p.layout) {
        case RawLayout::RGB: rgb = raw; break;
        case RawLayout::BGR: cv::cvtColor(raw, rgb, cv::COLOR_BGR2RGB); break;
        // OpenCV names Bayer patterns by the second row; *2RGB gives R,G,B order.
        case RawLayout::BayerRGGB: cv::demosaicing(raw, rgb, cv::COLOR_BayerBG2RGB); break;
        case RawLayout::BayerBGGR: cv::demosaicing(raw, rgb, cv::COLOR_BayerRG2RGB); break;
        case RawLayout::BayerGRBG: cv::demosaicing(raw, rgb, cv::COLOR_BayerGB2RGB); break;
        case RawLayout::BayerGBRG: cv::demosaicing(raw, rgb, cv::COLOR_BayerGR2RGB); break;
        case RawLayout::Mono: cv::cvtColor(raw, rgb, cv::COLOR_GRAY2RGB); break;
    }
    return finish(rgb, path, out, error);
}

cv::Mat downscale(const cv::Mat& img, int maxDim) {
    const int longSide = std::max(img.cols, img.rows);
    if (longSide <= maxDim) return img.clone();
    const double s = double(maxDim) / longSide;
    cv::Mat out;
    cv::resize(img, out, cv::Size(), s, s, cv::INTER_AREA);
    return out;
}

}  // namespace ccm
