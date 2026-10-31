#pragma once
#include <string>
#include <vector>

#include "core/CcmSolver.h"
#include "core/ChartSpec.h"
#include "core/Illuminant.h"
#include "core/PatchSampler.h"
#include "core/Targets.h"

namespace ccm {

struct PatchResult {
    int index = 0;
    bool included = false;
    cv::Vec3d cam;          // normalized camera RGB
    cv::Vec3d target;       // target linear RGB
    cv::Vec3d predicted;    // CCM output (linear)
    cv::Vec3d baseline;     // white-balance-only output (linear)
    cv::Vec3d targetLab, predictedLab;
    double de00 = 0, de76 = 0, dc = 0, de00Baseline = 0;
};

struct Stats {
    double mean = 0, median = 0, max = 0;
    int count = 0;
};

struct Evaluation {
    std::vector<PatchResult> patches;
    Stats de00, de76, dc, de00Baseline;
    double conditionNumber = 0;
    cv::Vec3d rowNorms;  // L2 norm of each matrix row (noise amplification)
    double maxAbsCoef = 0;
};

Evaluation evaluate(const FitResult& fit, const std::vector<cv::Vec3d>& cam, const Targets& targets,
                    const std::vector<bool>& included, const std::vector<bool>& neutral,
                    const std::string& outputSpace, bool useQuantized);

// Black level / linearity from neutral patches: DN = slope * Y + black per channel.
struct LinearityResult {
    bool ok = false;
    cv::Vec3d black{0, 0, 0};
    cv::Vec3d slope{0, 0, 0};
    double r2 = 0;        // worst channel
    double exponent = 0;  // log-log slope of (DN - black) vs Y (1.0 = linear)
};
LinearityResult analyzeLinearity(const std::vector<PatchStat>& stats, const ChartSpec& chart,
                                 const std::vector<bool>& included);

// Black level from the whole chart: per channel DN = a . targetLinear + black (least squares).
// Uses chromatic patches too, so it is not biased by slightly non-neutral gray patches.
bool estimateBlackFromChart(const std::vector<PatchStat>& stats, const Targets& targets,
                            const std::vector<bool>& included, cv::Vec3d& black);

struct SeparabilityIssue {
    int a = 0, b = 0;
    double distance = 0;   // camera chromaticity distance / pooled per-pixel noise
    double referenceDE = 0;
};
struct SeparabilityReport {
    std::vector<SeparabilityIssue> issues;  // pairs very different in reality but not in the camera
    double chromaRatio = 0;  // camera chroma / target chroma (median over chromatic patches)
};
SeparabilityReport analyzeSeparability(const std::vector<PatchStat>& stats, const Targets& targets,
                                       const std::vector<bool>& included, const std::vector<bool>& neutral,
                                       double minReferenceDE = 20.0, double maxDistance = 2.0);

struct ExportInfo {
    std::string chartName, illuminantName, outputSpaceName, imagePath;
    double cct = 0;
};
std::string formatMatrixText(const FitResult& fit);
std::string exportCHeader(const FitResult& fit, const ExportInfo& info);
std::string exportCsv(const FitResult& fit, const Evaluation& ev, const ChartSpec& chart);
std::string exportRegisters(const FitResult& fit, const ExportInfo& info);

}  // namespace ccm
