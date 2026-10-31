#include "core/PointMapping.h"

#include <cmath>

#include "core/ColorMath.h"

namespace ccm {

namespace {

double decode(double v, RefEncoding e) {
    switch (e) {
        case RefEncoding::SRGB: return srgbDecode(v);
        case RefEncoding::Gamma22: return std::pow(std::max(v, 0.0), 2.2);
        case RefEncoding::Linear: return v;
    }
    return v;
}

}  // namespace

ReferenceSample sampleReference(const cv::Mat& rgb, const Quad& quad, RefEncoding encoding,
                                double saturationFraction, double trimFraction) {
    ReferenceSample s;
    std::vector<double> ch[3];
    quadPixels(rgb, quad, ch);
    const size_t n = ch[0].size();
    s.pixelCount = int(n);
    if (n == 0) return s;

    const double maxValue = rgb.depth() == CV_8U ? 255.0 : 65535.0;
    // Decode through a table: 8-bit has 256 codes, 16-bit is decoded per value.
    std::vector<double> lut;
    if (rgb.depth() == CV_8U) {
        lut.resize(256);
        for (int i = 0; i < 256; ++i) lut[i] = decode(i / 255.0, encoding);
    }
    size_t saturated = 0;
    for (size_t i = 0; i < n; ++i) {
        bool sat = false;
        for (int k = 0; k < 3; ++k) {
            sat |= ch[k][i] >= saturationFraction * maxValue;
            ch[k][i] = lut.empty() ? decode(ch[k][i] / maxValue, encoding) : lut[size_t(ch[k][i])];
        }
        saturated += sat;
    }
    for (int k = 0; k < 3; ++k) {
        s.linear[k] = trimmedMean(ch[k], trimFraction);
        double sum = 0, sq = 0;
        for (double v : ch[k]) {
            sum += v;
            sq += v * v;
        }
        const double m = sum / n;
        s.stdDev[k] = std::sqrt(std::max(sq / n - m * m, 0.0));
    }
    s.saturatedRatio = double(saturated) / n;
    return s;
}

PointTargets buildPointTargets(const std::vector<cv::Vec3d>& refLinear, const std::vector<cv::Vec3d>& cam,
                               const std::vector<bool>& neutral, const std::vector<bool>& included,
                               const PointTargetOptions& opt) {
    PointTargets res;
    const size_t n = refLinear.size();
    std::vector<size_t> neutrals, used;
    for (size_t i = 0; i < n; ++i) {
        if (!included[i]) continue;
        used.push_back(i);
        if (neutral[i]) neutrals.push_back(i);
    }
    if (opt.neutralizeReference && neutrals.empty()) {
        res.message = "Mark at least one gray/white point to neutralize the reference.";
        return res;
    }

    // 1) neutralize: the neutral points of the reference become gray (R = G = B).
    //    Each point is normalized first so that its (unreliable) brightness does not weight it.
    std::vector<cv::Vec3d> ref = refLinear;
    if (opt.neutralizeReference) {
        cv::Vec3d sum(0, 0, 0);
        for (size_t i : neutrals) {
            const double m = (refLinear[i][0] + refLinear[i][1] + refLinear[i][2]) / 3.0;
            if (m > 1e-9) sum += refLinear[i] / m;
        }
        const double mean = (sum[0] + sum[1] + sum[2]) / 3.0;
        for (int c = 0; c < 3; ++c) res.referenceGains[c] = sum[c] > 0 ? mean / sum[c] : 1.0;
        for (auto& r : ref) r = r.mul(res.referenceGains);
    }

    // 2) reference is sRGB primaries; express it in the output space
    const RgbSpace& srgb = rgbSpace("srgb");
    const RgbSpace& out = rgbSpace(opt.outputSpace);
    const Mat33 toOut = out.xyzToRgb * srgb.rgbToXyz;
    std::vector<cv::Vec3d> t(n);
    for (size_t i = 0; i < n; ++i) t[i] = toOut * ref[i];

    // 3) chromaticity only: the reference brightness of each point is unknown, so solve for a
    //    per-point scale s_i together with a linear camera model, min sum |M cam_i - s_i t_i|^2,
    //    by alternating least squares. The targets keep their chromaticity and get the
    //    brightness that is consistent with the camera.
    if (!opt.matchBrightness && used.size() >= 4) {
        std::vector<double> s(n, 1.0);
        // start from the white-balanced camera luminance (exact for neutral points)
        const std::vector<size_t>& gainSet = neutrals.empty() ? used : neutrals;
        cv::Vec3d g(1, 1, 1);
        for (int c = 0; c < 3; ++c) {
            double num = 0, den = 0;
            for (size_t i : gainSet) {
                num += t[i][c] * cam[i][c];
                den += cam[i][c] * cam[i][c];
            }
            if (den > 0 && num > 0) g[c] = num / den;
        }
        const cv::Vec3d yRow(out.rgbToXyz(1, 0), out.rgbToXyz(1, 1), out.rgbToXyz(1, 2));
        for (size_t i : used) {
            const double yt = yRow.dot(t[i]), yc = yRow.dot(cam[i].mul(g));
            if (yt > 1e-9 && yc > 0) s[i] = yc / yt;
        }

        cv::Mat A(int(used.size()), 3, CV_64F), B(int(used.size()), 3, CV_64F);
        for (int k = 0; k < int(used.size()); ++k)
            for (int c = 0; c < 3; ++c) A.at<double>(k, c) = cam[used[k]][c];
        for (int it = 0; it < 200; ++it) {
            // keep the overall scale fixed (otherwise s -> 0, M -> 0)
            double mean = 0;
            for (size_t i : used) mean += s[i];
            mean /= double(used.size());
            if (mean <= 0) break;
            for (size_t i : used) s[i] /= mean;

            for (int k = 0; k < int(used.size()); ++k)
                for (int c = 0; c < 3; ++c) B.at<double>(k, c) = s[used[k]] * t[used[k]][c];
            cv::Mat Mt;  // 3x3, cam row -> target row
            if (!cv::solve(A, B, Mt, cv::DECOMP_SVD)) break;
            double change = 0;
            for (int k = 0; k < int(used.size()); ++k) {
                const size_t i = used[k];
                cv::Mat pred = A.row(k) * Mt;
                const cv::Vec3d p(pred.at<double>(0), pred.at<double>(1), pred.at<double>(2));
                const double tt = t[i].dot(t[i]);
                const double ns = tt > 1e-12 ? std::max(p.dot(t[i]) / tt, 1e-6) : s[i];
                change += std::abs(ns - s[i]);
                s[i] = ns;
            }
            if (change / double(used.size()) < 1e-9) break;
        }
        for (size_t i : used) t[i] *= s[i];
    }

    for (size_t i = 0; i < n; ++i) {
        res.targets.linearRgb.push_back(t[i]);
        res.targets.lab.push_back(linearRgbToLab(t[i], out));
    }
    res.ok = true;
    return res;
}

}  // namespace ccm
