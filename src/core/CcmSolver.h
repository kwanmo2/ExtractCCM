#pragma once
#include <opencv2/core.hpp>
#include <string>
#include <vector>

#include "core/ColorMath.h"

namespace ccm {

enum class Model {
    M3x3,          // out = k * M * in
    M3x3RowSum,    // white-preserving: camera neutral maps exactly to target neutral
    M3x4,          // out = k * (M * in + b)
    WbM3x3,        // out = M * diag(wb) * in, wb gains from neutral patches, M white-preserving
    RootPoly3x6,   // out = k * M * [r g b sqrt(rg) sqrt(gb) sqrt(rb)]  (software pipelines)
};

enum class Objective { LinearLS, DeltaE2000 };
enum class OffsetMode { Free, NonNegative };

struct QuantOptions {
    bool enabled = false;
    int intBits = 4;          // integer bits (excluding sign)
    int fracBits = 8;         // fractional bits
    bool isSigned = true;
    bool preserveRowSum = true;
    int offsetBits = 12;      // offset register expressed in output DN at this bit depth
};

struct FitOptions {
    Model model = Model::M3x3;
    Objective objective = Objective::LinearLS;
    bool separateGain = true;  // pull exposure gain k out of the matrix
    bool boundCoefficients = false;
    double coefMin = -16.0;
    double coefMax = 16.0;
    OffsetMode offsetMode = OffsetMode::Free;
    double neutralWeight = 1.0;
    QuantOptions quant;
    std::string outputSpace = "srgb";  // for the ΔE2000 objective
    int maxIterations = 200;
};

struct FitResult {
    bool ok = false;
    std::string message;
    Model model = Model::M3x3;
    cv::Mat coef;                 // 3 x K, CV_64F (float values)
    cv::Vec3d wbGains{1, 1, 1};
    double gain = 1.0;            // exposure gain k
    bool quantized = false;
    cv::Mat coefQuantized;        // 3 x K, dequantized values actually used by hardware
    cv::Mat coefRegisters;        // 3 x K, CV_32S register integers (matrix part)
    std::vector<int> offsetDN;    // M3x4 only: offset in output DN (quant.offsetBits)
    QuantOptions quant;
};

int featureCount(Model m);
bool hasOffset(Model m);
bool hasRowSumConstraint(Model m);
const char* modelName(Model m);
std::vector<double> features(Model m, const cv::Vec3d& rgb);

// Applies the fitted transform to a normalized, black-subtracted camera RGB value.
cv::Vec3d applyFit(const FitResult& fit, const cv::Vec3d& cam, bool useQuantized);

// cam: camera RGB (normalized, black subtracted); target: linear output RGB;
// targetLab: for the ΔE objective; neutral/weight: per patch; patches with weight <= 0 are ignored.
FitResult fitCcm(const std::vector<cv::Vec3d>& cam, const std::vector<cv::Vec3d>& target,
                 const std::vector<cv::Vec3d>& targetLab, const std::vector<bool>& neutral,
                 const std::vector<double>& weight, const FitOptions& opt);

// Bounded least squares for one output row: min ||A x - b||^2, lb <= x <= ub,
// optionally eq . x == eqValue (eq empty = no equality). Exact active-set enumeration (K <= 7).
cv::Mat solveRowBounded(const cv::Mat& A, const cv::Mat& b, const std::vector<double>& lb,
                        const std::vector<double>& ub, const std::vector<double>& eq, double eqValue);

}  // namespace ccm
