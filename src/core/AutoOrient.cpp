#include "core/AutoOrient.h"

#include <algorithm>
#include <limits>

#include "core/CcmSolver.h"
#include "core/Report.h"

namespace ccm {

std::vector<OrientationCandidate> rankOrientations(const cv::Mat& rgb, const ChartCorners& clicked,
                                                   const SampleSettings& settings, const ChartSpec& chart,
                                                   const Targets& targets, const std::vector<bool>& excluded) {
    std::vector<bool> neutral(chart.count(), false);
    for (int i : chart.neutralIndices()) neutral[i] = true;

    // Independent of the black level: a black level estimated with a wrong patch order is itself
    // wrong, so sample raw DN and let the offset column of a 3x4 fit absorb the black level.
    SampleSettings raw = settings;
    raw.black = cv::Vec3d(0, 0, 0);

    std::vector<OrientationCandidate> out;
    for (int flip = 0; flip < 2; ++flip)
        for (int turns = 0; turns < 4; ++turns) {
            OrientationCandidate c;
            c.flipped = flip == 1;
            c.quarterTurns = turns;
            c.corners = rotateCorners(c.flipped ? flipCorners(clicked) : clicked, turns);

            const auto stats = samplePatches(rgb, c.corners, raw);
            std::vector<cv::Vec3d> cam;
            std::vector<double> weight;
            std::vector<bool> included;
            for (int i = 0; i < chart.count(); ++i) {
                const bool use = chart.patches[i].hasRef && stats[i].pixelCount > 0 &&
                                 stats[i].saturatedRatio <= 0.01 && !(i < int(excluded.size()) && excluded[i]);
                cam.push_back(stats[i].mean);
                weight.push_back(use ? 1.0 : 0.0);
                included.push_back(use);
                c.usedPatches += use;
            }
            c.meanDeltaE = std::numeric_limits<double>::infinity();
            FitOptions o;
            o.model = Model::M3x4;
            const FitResult fit = fitCcm(cam, targets.linearRgb, targets.lab, neutral, weight, o);
            if (fit.ok) c.meanDeltaE = evaluate(fit, cam, targets, included, neutral, o.outputSpace, false).de00.mean;
            out.push_back(c);
        }
    std::stable_sort(out.begin(), out.end(), [](const OrientationCandidate& a, const OrientationCandidate& b) {
        return a.meanDeltaE < b.meanDeltaE;
    });
    return out;
}

}  // namespace ccm
