#include "core/PatchSampler.h"

#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>

namespace ccm {

cv::Vec2d chromaOf(const cv::Vec3d& v) {
    const double s = v[0] + v[1] + v[2];
    if (s <= 1e-9) return cv::Vec2d(0, 0);
    return cv::Vec2d((v[0] - v[1]) / s, ((v[0] + v[1]) / 2.0 - v[2]) / s);
}

cv::Matx33d gridToImage(const ChartCorners& corners, int rows, int cols) {
    const float c1 = float(std::max(cols - 1, 1)), r1 = float(std::max(rows - 1, 1));
    cv::Point2f src[4] = {{0, 0}, {c1, 0}, {c1, r1}, {0, r1}};
    cv::Point2f dst[4] = {corners[0], corners[1], corners[2], corners[3]};
    cv::Mat h = cv::getPerspectiveTransform(src, dst);
    return cv::Matx33d(h);
}

std::vector<Quad> patchQuads(const ChartCorners& corners, int rows, int cols, double fraction) {
    const cv::Matx33d h = gridToImage(corners, rows, cols);
    auto map = [&](double x, double y) {
        cv::Vec3d p = h * cv::Vec3d(x, y, 1.0);
        return cv::Point2f(float(p[0] / p[2]), float(p[1] / p[2]));
    };
    const double hf = fraction / 2.0;
    std::vector<Quad> quads;
    quads.reserve(size_t(rows) * cols);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            quads.push_back({map(c - hf, r - hf), map(c + hf, r - hf), map(c + hf, r + hf), map(c - hf, r + hf)});
    return quads;
}

namespace {

template <typename T>
void collect(const cv::Mat& roi, const cv::Mat& mask, std::vector<double> ch[3]) {
    for (int y = 0; y < roi.rows; ++y) {
        const auto* p = roi.ptr<cv::Vec<T, 3>>(y);
        const auto* m = mask.ptr<uchar>(y);
        for (int x = 0; x < roi.cols; ++x) {
            if (!m[x]) continue;
            for (int k = 0; k < 3; ++k) ch[k].push_back(double(p[x][k]));
        }
    }
}

}  // namespace

double trimmedMean(std::vector<double> v, double trim) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t cut = size_t(std::floor(v.size() * trim));
    if (2 * cut >= v.size()) cut = 0;
    double sum = 0.0;
    for (size_t i = cut; i < v.size() - cut; ++i) sum += v[i];
    return sum / double(v.size() - 2 * cut);
}

void quadPixels(const cv::Mat& rgb, const Quad& q, std::vector<double> ch[3]) {
    CV_Assert(rgb.type() == CV_8UC3 || rgb.type() == CV_16UC3);
    for (int k = 0; k < 3; ++k) ch[k].clear();
    std::vector<cv::Point2f> pts(q.begin(), q.end());
    const cv::Rect box = cv::boundingRect(pts) & cv::Rect(0, 0, rgb.cols, rgb.rows);
    if (box.area() == 0) return;
    cv::Mat mask = cv::Mat::zeros(box.size(), CV_8U);
    constexpr int kShift = 4;
    std::vector<cv::Point> ip;
    for (const auto& p : q)
        ip.emplace_back(cvRound((p.x - box.x) * (1 << kShift)), cvRound((p.y - box.y) * (1 << kShift)));
    cv::fillConvexPoly(mask, ip.data(), int(ip.size()), cv::Scalar(255), cv::LINE_8, kShift);
    const cv::Mat roi = rgb(box);
    if (rgb.depth() == CV_8U) collect<uchar>(roi, mask, ch);
    else collect<ushort>(roi, mask, ch);
}

PatchStat sampleQuad(const cv::Mat& rgb, const Quad& q, const SampleSettings& s) {
    const cv::Vec3d range(std::max(s.fullScale - s.black[0], 1.0), std::max(s.fullScale - s.black[1], 1.0),
                          std::max(s.fullScale - s.black[2], 1.0));
    const double satLevel = s.saturationFraction * s.fullScale;

    PatchStat st;
    st.quad = q;
    std::vector<double> ch[3];
    quadPixels(rgb, q, ch);
    const size_t n = ch[0].size();
    st.pixelCount = int(n);
    if (n == 0) return st;

    size_t saturated = 0;
    cv::Vec2d cSum(0, 0), cSq(0, 0);
    cv::Vec3d sum(0, 0, 0), sq(0, 0, 0);
    for (size_t i = 0; i < n; ++i) {
        if (ch[0][i] >= satLevel || ch[1][i] >= satLevel || ch[2][i] >= satLevel) ++saturated;
        cv::Vec3d v;
        for (int k = 0; k < 3; ++k) {
            v[k] = (ch[k][i] - s.black[k]) / range[k];
            sum[k] += v[k];
            sq[k] += v[k] * v[k];
        }
        cv::Vec2d c = chromaOf(v);
        cSum += c;
        cSq += c.mul(c);
    }
    for (int k = 0; k < 3; ++k) {
        st.meanDN[k] = trimmedMean(ch[k], s.trimFraction);
        st.mean[k] = (st.meanDN[k] - s.black[k]) / range[k];
        const double m = sum[k] / n;
        st.stdDev[k] = std::sqrt(std::max(sq[k] / n - m * m, 0.0));
    }
    st.saturatedRatio = double(saturated) / n;
    st.chroma = chromaOf(st.mean);
    const cv::Vec2d cm = cSum / double(n);
    const double v0 = std::max(cSq[0] / n - cm[0] * cm[0], 0.0);
    const double v1 = std::max(cSq[1] / n - cm[1] * cm[1], 0.0);
    st.chromaNoise = std::sqrt((v0 + v1) / 2.0);
    return st;
}

std::vector<PatchStat> samplePatches(const cv::Mat& rgb, const ChartCorners& corners, const SampleSettings& s) {
    std::vector<PatchStat> out;
    for (const auto& q : patchQuads(corners, s.rows, s.cols, s.sampleFraction)) out.push_back(sampleQuad(rgb, q, s));
    return out;
}

Quad squareAround(cv::Point2f c, double halfSize) {
    const float h = float(halfSize);
    return {cv::Point2f(c.x - h, c.y - h), cv::Point2f(c.x + h, c.y - h), cv::Point2f(c.x + h, c.y + h),
            cv::Point2f(c.x - h, c.y + h)};
}

ChartCorners rotateCorners(const ChartCorners& c, int quarterTurns) {
    ChartCorners r = c;
    const int k = ((quarterTurns % 4) + 4) % 4;
    for (int i = 0; i < 4; ++i) r[i] = c[(i + k) % 4];
    return r;
}

ChartCorners flipCorners(const ChartCorners& c) { return {c[1], c[0], c[3], c[2]}; }

}  // namespace ccm
