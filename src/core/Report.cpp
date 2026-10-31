#include "core/Report.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace ccm {

namespace {

Stats makeStats(std::vector<double> v) {
    Stats s;
    s.count = int(v.size());
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    double sum = 0;
    for (double x : v) sum += x;
    s.mean = sum / v.size();
    s.median = v.size() % 2 ? v[v.size() / 2] : (v[v.size() / 2 - 1] + v[v.size() / 2]) / 2.0;
    s.max = v.back();
    return s;
}

std::string fmt(const char* f, double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), f, v);
    return buf;
}

std::string hex(int v, int bits) {
    const unsigned mask = bits >= 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%0*X", (bits + 3) / 4, unsigned(v) & mask);
    return buf;
}

int registerBits(const QuantOptions& q) { return q.intBits + q.fracBits + (q.isSigned ? 1 : 0); }

}  // namespace

Evaluation evaluate(const FitResult& fit, const std::vector<cv::Vec3d>& cam, const Targets& targets,
                    const std::vector<bool>& included, const std::vector<bool>& neutral,
                    const std::string& outputSpace, bool useQuantized) {
    const RgbSpace& space = rgbSpace(outputSpace);
    Evaluation ev;

    // White-balance-only baseline: per-channel gains from neutral patches.
    cv::Vec3d wb(1, 1, 1);
    for (int c = 0; c < 3; ++c) {
        double num = 0, den = 0;
        for (size_t i = 0; i < cam.size(); ++i) {
            if (!included[i] || !neutral[i]) continue;
            num += targets.linearRgb[i][c] * cam[i][c];
            den += cam[i][c] * cam[i][c];
        }
        if (den > 0) wb[c] = num / den;
    }

    std::vector<double> d00, d76, dc, dBase;
    for (size_t i = 0; i < cam.size(); ++i) {
        PatchResult r;
        r.index = int(i);
        r.included = included[i];
        r.cam = cam[i];
        r.target = targets.linearRgb[i];
        r.targetLab = targets.lab[i];
        r.predicted = applyFit(fit, cam[i], useQuantized);
        r.baseline = cam[i].mul(wb);
        r.predictedLab = linearRgbToLab(r.predicted, space);
        r.de00 = deltaE2000(r.predictedLab, r.targetLab);
        r.de76 = deltaE76(r.predictedLab, r.targetLab);
        r.dc = deltaC(r.predictedLab, r.targetLab);
        r.de00Baseline = deltaE2000(linearRgbToLab(r.baseline, space), r.targetLab);
        if (r.included) {
            d00.push_back(r.de00);
            d76.push_back(r.de76);
            dc.push_back(r.dc);
            dBase.push_back(r.de00Baseline);
        }
        ev.patches.push_back(r);
    }
    ev.de00 = makeStats(d00);
    ev.de76 = makeStats(d76);
    ev.dc = makeStats(dc);
    ev.de00Baseline = makeStats(dBase);

    const cv::Mat& coef = (useQuantized && fit.quantized) ? fit.coefQuantized : fit.coef;
    if (!coef.empty()) {
        cv::Mat m = coef(cv::Rect(0, 0, 3, 3)).clone();
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) m.at<double>(r, c) *= fit.gain * fit.wbGains[c];
        cv::Mat w;
        cv::SVD::compute(m, w);
        ev.conditionNumber = w.at<double>(2) > 1e-12 ? w.at<double>(0) / w.at<double>(2) : INFINITY;
        for (int r = 0; r < 3; ++r) ev.rowNorms[r] = cv::norm(m.row(r));
        double mx = 0;
        for (int r = 0; r < coef.rows; ++r)
            for (int c = 0; c < coef.cols; ++c)
                if (!(hasOffset(fit.model) && c == 3)) mx = std::max(mx, std::abs(coef.at<double>(r, c)));
        ev.maxAbsCoef = mx;
    }
    return ev;
}

LinearityResult analyzeLinearity(const std::vector<PatchStat>& stats, const ChartSpec& chart,
                                 const std::vector<bool>& included) {
    std::vector<double> refY(chart.count(), 0.0);
    std::vector<bool> neutral(chart.count(), false);
    for (int i : chart.neutralIndices()) {
        refY[i] = labToXyz(chart.patches[i].labD50, whiteD50())[1];
        neutral[i] = true;
    }
    return analyzeLinearity(stats, refY, neutral, included);
}

LinearityResult analyzeLinearity(const std::vector<PatchStat>& stats, const std::vector<double>& refY,
                                 const std::vector<bool>& neutral, const std::vector<bool>& included) {
    LinearityResult res;
    std::vector<int> idx;
    for (int i = 0; i < int(stats.size()) && i < int(refY.size()); ++i)
        if (neutral[i] && included[i] && stats[i].pixelCount > 0 && refY[i] > 0) idx.push_back(i);
    if (idx.size() < 3) return res;

    std::vector<double> Y;
    for (int i : idx) Y.push_back(refY[i]);

    res.r2 = 1.0;
    double expSum = 0;
    int expCount = 0;
    for (int c = 0; c < 3; ++c) {
        const size_t n = idx.size();
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (size_t k = 0; k < n; ++k) {
            const double x = Y[k], y = stats[idx[k]].meanDN[c];
            sx += x; sy += y; sxx += x * x; sxy += x * y;
        }
        const double den = n * sxx - sx * sx;
        if (std::abs(den) < 1e-12) return res;
        const double slope = (n * sxy - sx * sy) / den;
        const double icpt = (sy - slope * sx) / n;
        double ssr = 0, sst = 0;
        const double my = sy / n;
        for (size_t k = 0; k < n; ++k) {
            const double y = stats[idx[k]].meanDN[c];
            ssr += std::pow(y - (slope * Y[k] + icpt), 2);
            sst += std::pow(y - my, 2);
        }
        res.slope[c] = slope;
        res.black[c] = std::max(icpt, 0.0);
        res.r2 = std::min(res.r2, sst > 0 ? 1.0 - ssr / sst : 0.0);

        // log-log exponent between darkest and brightest neutral
        auto mm = std::minmax_element(Y.begin(), Y.end());
        const double y0 = stats[idx[mm.first - Y.begin()]].meanDN[c] - res.black[c];
        const double y1 = stats[idx[mm.second - Y.begin()]].meanDN[c] - res.black[c];
        if (y0 > 0 && y1 > 0) {
            expSum += std::log(y1 / y0) / std::log(*mm.second / *mm.first);
            ++expCount;
        }
    }
    res.exponent = expCount ? expSum / expCount : 0;
    res.ok = true;
    return res;
}

bool estimateBlackFromChart(const std::vector<PatchStat>& stats, const Targets& targets,
                            const std::vector<bool>& included, cv::Vec3d& black) {
    std::vector<int> idx;
    for (int i = 0; i < int(stats.size()); ++i)
        if (included[i] && stats[i].pixelCount > 0) idx.push_back(i);
    if (idx.size() < 6) return false;
    cv::Mat A(int(idx.size()), 4, CV_64F);
    for (int k = 0; k < int(idx.size()); ++k) {
        const cv::Vec3d& t = targets.linearRgb[idx[k]];
        A.at<double>(k, 0) = t[0];
        A.at<double>(k, 1) = t[1];
        A.at<double>(k, 2) = t[2];
        A.at<double>(k, 3) = 1.0;
    }
    for (int c = 0; c < 3; ++c) {
        cv::Mat b(int(idx.size()), 1, CV_64F), x;
        for (int k = 0; k < int(idx.size()); ++k) b.at<double>(k) = stats[idx[k]].meanDN[c];
        if (!cv::solve(A, b, x, cv::DECOMP_SVD)) return false;
        black[c] = std::max(x.at<double>(3), 0.0);
    }
    return true;
}

SeparabilityReport analyzeSeparability(const std::vector<PatchStat>& stats, const Targets& targets,
                                       const std::vector<bool>& included, const std::vector<bool>& neutral,
                                       double minReferenceDE, double maxDistance) {
    SeparabilityReport rep;
    const size_t n = stats.size();

    // White balance the camera chromaticity on neutral patches before comparing.
    cv::Vec3d wb(1, 1, 1), sum(0, 0, 0);
    for (size_t i = 0; i < n; ++i)
        if (included[i] && neutral[i]) sum += stats[i].mean;
    if (sum[0] > 0 && sum[1] > 0 && sum[2] > 0) wb = cv::Vec3d(sum[1] / sum[0], 1.0, sum[1] / sum[2]);

    std::vector<cv::Vec2d> chroma(n);
    for (size_t i = 0; i < n; ++i) chroma[i] = chromaOf(stats[i].mean.mul(wb));

    std::vector<double> ratios;
    for (size_t i = 0; i < n; ++i) {
        if (!included[i] || neutral[i]) continue;
        const double tc = cv::norm(chromaOf(targets.linearRgb[i]));
        if (tc > 0.05) ratios.push_back(cv::norm(chroma[i]) / tc);
    }
    if (!ratios.empty()) {
        std::sort(ratios.begin(), ratios.end());
        rep.chromaRatio = ratios[ratios.size() / 2];
    }

    for (size_t a = 0; a < n; ++a) {
        if (!included[a]) continue;
        for (size_t b = a + 1; b < n; ++b) {
            if (!included[b]) continue;
            const double refDE = deltaE2000(targets.lab[a], targets.lab[b]);
            if (refDE < minReferenceDE) continue;
            const double noise = std::sqrt((std::pow(stats[a].chromaNoise, 2) + std::pow(stats[b].chromaNoise, 2)) / 2.0);
            // Lightness differs too; compare chromaticity only because that is what a CCM can rotate.
            const double d = cv::norm(chroma[a] - chroma[b]) / std::max(noise, 1e-6);
            const double lightDiff = std::abs(targets.lab[a][0] - targets.lab[b][0]);
            if (d < maxDistance && lightDiff < minReferenceDE) rep.issues.push_back({int(a), int(b), d, refDE});
        }
    }
    std::sort(rep.issues.begin(), rep.issues.end(),
              [](const SeparabilityIssue& x, const SeparabilityIssue& y) { return x.distance < y.distance; });
    return rep;
}

std::string formatMatrixText(const FitResult& fit) {
    std::ostringstream os;
    const int K = fit.coef.cols;
    os << "Model: " << modelName(fit.model) << "\n";
    if (fit.model == Model::WbM3x3)
        os << "WB gains (R,G,B): " << fmt("%.5f", fit.wbGains[0]) << ", " << fmt("%.5f", fit.wbGains[1]) << ", "
           << fmt("%.5f", fit.wbGains[2]) << "\n";
    os << "Exposure gain k: " << fmt("%.5f", fit.gain) << "\n";
    os << "out = k * (M * " << (fit.model == Model::RootPoly3x6 ? "f(in)" : "in") << (hasOffset(fit.model) ? " + b" : "")
       << ")\n\nM" << (hasOffset(fit.model) ? " | b" : "") << " (float):\n";
    for (int r = 0; r < 3; ++r) {
        os << "  [";
        for (int j = 0; j < K; ++j) {
            if (hasOffset(fit.model) && j == 3) os << " |";
            os << fmt(" %10.5f", fit.coef.at<double>(r, j));
        }
        double s = fit.coef.at<double>(r, 0) + fit.coef.at<double>(r, 1) + fit.coef.at<double>(r, 2);
        os << " ]   row sum " << fmt("%.4f", s) << "\n";
    }
    if (fit.quantized) {
        const auto& q = fit.quant;
        os << "\nQuantized (" << (q.isSigned ? "S" : "U") << q.intBits << "." << q.fracBits << ", "
           << registerBits(q) << " bits):\n";
        for (int r = 0; r < 3; ++r) {
            os << "  [";
            for (int j = 0; j < fit.coefRegisters.cols; ++j)
                os << " " << fmt("%7.0f", fit.coefRegisters.at<int>(r, j)) << " (" << hex(fit.coefRegisters.at<int>(r, j), registerBits(q)) << ")";
            os << " ]\n";
        }
        if (!fit.offsetDN.empty())
            os << "  Offset (DN @" << q.offsetBits << "bit): " << fit.offsetDN[0] << ", " << fit.offsetDN[1] << ", "
               << fit.offsetDN[2] << "\n";
    }
    return os.str();
}

std::string exportCHeader(const FitResult& fit, const ExportInfo& info) {
    std::ostringstream os;
    const int K = fit.coef.cols;
    os << "// Color correction matrix generated by ExtractCCM\n"
       << "// Model      : " << modelName(fit.model) << "\n"
       << "// Chart      : " << info.chartName << "\n"
       << "// Illuminant : " << info.illuminantName << " (" << fmt("%.0f", info.cct) << " K)\n"
       << "// Output     : " << info.outputSpaceName << " (linear)\n"
       << "// Input      : black-subtracted, normalized to [0,1]\n"
       << "// out = CCM_GAIN * (CCM * " << (fit.model == Model::RootPoly3x6 ? "[r g b sqrt(rg) sqrt(gb) sqrt(rb)]" : "in")
       << (hasOffset(fit.model) ? " + offset" : "") << ")\n#pragma once\n\n";
    os << "static const float CCM_WB_GAINS[3] = { " << fmt("%.6ff", fit.wbGains[0]) << ", " << fmt("%.6ff", fit.wbGains[1])
       << ", " << fmt("%.6ff", fit.wbGains[2]) << " };\n";
    os << "static const float CCM_GAIN = " << fmt("%.6ff", fit.gain) << ";\n";
    os << "static const float CCM[3][" << K << "] = {\n";
    for (int r = 0; r < 3; ++r) {
        os << "    {";
        for (int j = 0; j < K; ++j) os << (j ? ", " : " ") << fmt("%.6ff", fit.coef.at<double>(r, j));
        os << " },\n";
    }
    os << "};\n";
    if (fit.quantized) {
        os << "\n// Fixed point " << (fit.quant.isSigned ? "S" : "U") << fit.quant.intBits << "." << fit.quant.fracBits << "\n";
        os << "static const int CCM_REG[3][" << fit.coefRegisters.cols << "] = {\n";
        for (int r = 0; r < 3; ++r) {
            os << "    {";
            for (int j = 0; j < fit.coefRegisters.cols; ++j) os << (j ? ", " : " ") << fit.coefRegisters.at<int>(r, j);
            os << " },\n";
        }
        os << "};\n";
        if (!fit.offsetDN.empty())
            os << "static const int CCM_OFFSET_DN[3] = { " << fit.offsetDN[0] << ", " << fit.offsetDN[1] << ", "
               << fit.offsetDN[2] << " };  // @" << fit.quant.offsetBits << " bit\n";
    }
    return os.str();
}

std::string exportCsv(const FitResult& fit, const Evaluation& ev, const std::vector<std::string>& names) {
    std::ostringstream os;
    os << "section,row,c0,c1,c2,c3,c4,c5\n";
    for (int r = 0; r < 3; ++r) {
        os << "matrix," << r;
        for (int j = 0; j < fit.coef.cols; ++j) os << "," << fmt("%.8f", fit.coef.at<double>(r, j));
        os << "\n";
    }
    os << "gain,0," << fmt("%.8f", fit.gain) << "\n";
    os << "wb,0," << fmt("%.8f", fit.wbGains[0]) << "," << fmt("%.8f", fit.wbGains[1]) << "," << fmt("%.8f", fit.wbGains[2]) << "\n\n";
    os << "patch,name,included,cam_r,cam_g,cam_b,target_r,target_g,target_b,out_r,out_g,out_b,"
          "target_L,target_a,target_b,out_L,out_a,out_b,dE00,dE76,dC,dE00_wb_only\n";
    for (const auto& p : ev.patches) {
        os << p.index + 1 << ",\"" << names[p.index] << "\"," << (p.included ? 1 : 0);
        for (const cv::Vec3d* v : {&p.cam, &p.target, &p.predicted, &p.targetLab, &p.predictedLab})
            for (int k = 0; k < 3; ++k) os << "," << fmt("%.6f", (*v)[k]);
        os << "," << fmt("%.4f", p.de00) << "," << fmt("%.4f", p.de76) << "," << fmt("%.4f", p.dc) << ","
           << fmt("%.4f", p.de00Baseline) << "\n";
    }
    return os.str();
}

std::string exportRegisters(const FitResult& fit, const ExportInfo& info) {
    std::ostringstream os;
    os << "# CCM register values - " << modelName(fit.model) << ", " << info.illuminantName << "\n";
    if (!fit.quantized) {
        os << "# Quantization disabled: enable it to get register values.\n";
        return os.str();
    }
    const int bits = registerBits(fit.quant);
    os << "# Format " << (fit.quant.isSigned ? "S" : "U") << fit.quant.intBits << "." << fit.quant.fracBits << " ("
       << bits << " bits, two's complement)\n";
    os << "# Separate exposure gain (apply in sensor/ISP gain): " << fmt("%.6f", fit.gain) << "\n";
    if (fit.model == Model::WbM3x3)
        os << "# WB gains: " << fmt("%.6f", fit.wbGains[0]) << " " << fmt("%.6f", fit.wbGains[1]) << " "
           << fmt("%.6f", fit.wbGains[2]) << "\n";
    for (int r = 0; r < 3; ++r)
        for (int j = 0; j < fit.coefRegisters.cols; ++j)
            os << "CCM_" << "RGB"[r] << j << " = " << hex(fit.coefRegisters.at<int>(r, j), bits) << "  # "
               << fit.coefRegisters.at<int>(r, j) << " = " << fmt("%.6f", fit.coefQuantized.at<double>(r, j)) << "\n";
    for (size_t r = 0; r < fit.offsetDN.size(); ++r)
        os << "CCM_OFFSET_" << "RGB"[r] << " = " << fit.offsetDN[r] << "  # DN @" << fit.quant.offsetBits << " bit\n";
    return os.str();
}

}  // namespace ccm
