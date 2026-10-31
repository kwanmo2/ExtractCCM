// Minimal self-contained test runner (no external test framework needed).
#include <cmath>
#include <cstdio>
#include <functional>
#include <opencv2/imgproc.hpp>
#include <random>
#include <string>
#include <vector>

#include "core/AutoOrient.h"
#include "core/CcmSolver.h"
#include "core/ChartSpec.h"
#include "core/ColorMath.h"
#include "core/Illuminant.h"
#include "core/PatchSampler.h"
#include "core/Report.h"
#include "core/Targets.h"

using namespace ccm;

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)
#define CHECK_NEAR(a, b, tol)                                                                          \
    do {                                                                                               \
        double _a = (a), _b = (b);                                                                     \
        if (!(std::abs(_a - _b) <= (tol))) {                                                           \
            std::printf("  FAIL %s:%d: %s = %.6f, expected %.6f (tol %g)\n", __FILE__, __LINE__, #a, _a, _b, \
                        double(tol));                                                                  \
            ++g_failures;                                                                              \
        }                                                                                              \
    } while (0)

// Sharma, Wu, Dalal (2005) CIEDE2000 test data (subset).
static void testDeltaE2000() {
    struct Case { double L1, a1, b1, L2, a2, b2, de; };
    const Case cases[] = {
        {50.0000, 2.6772, -79.7751, 50.0000, 0.0000, -82.7485, 2.0425},
        {50.0000, 3.1571, -77.2803, 50.0000, 0.0000, -82.7485, 2.8615},
        {50.0000, 2.8361, -74.0200, 50.0000, 0.0000, -82.7485, 3.4412},
        {50.0000, -1.3802, -84.2814, 50.0000, 0.0000, -82.7485, 1.0000},
        {50.0000, 0.0000, 0.0000, 50.0000, -1.0000, 2.0000, 2.3669},
        {50.0000, 2.5000, 0.0000, 73.0000, 25.0000, -18.0000, 27.1492},
        {50.0000, 2.5000, 0.0000, 61.0000, -5.0000, 29.0000, 22.8977},
        {50.0000, 2.5000, 0.0000, 56.0000, -27.0000, -3.0000, 31.9030},
        {50.0000, 2.5000, 0.0000, 58.0000, 24.0000, 15.0000, 19.4535},
        {60.2574, -34.0099, 36.2677, 60.4626, -34.1751, 39.4387, 1.2644},
    };
    for (const auto& c : cases)
        CHECK_NEAR(deltaE2000(Vec3(c.L1, c.a1, c.b1), Vec3(c.L2, c.a2, c.b2)), c.de, 1e-4);
}

static void testColorConversions() {
    const RgbSpace& s = rgbSpace("srgb");
    Vec3 w = s.xyzToRgb * whiteD65();
    CHECK_NEAR(w[0], 1.0, 1e-3);
    CHECK_NEAR(w[1], 1.0, 1e-3);
    CHECK_NEAR(w[2], 1.0, 1e-3);
    Vec3 lab(50, 20, -30);
    Vec3 back = xyzToLab(labToXyz(lab, whiteD50()), whiteD50());
    CHECK_NEAR(deltaE76(lab, back), 0.0, 1e-9);
    // Bradford maps source white onto destination white
    Vec3 d = bradford(whiteD50(), whiteD65()) * whiteD50();
    CHECK_NEAR(cv::norm(d - whiteD65()), 0.0, 1e-9);

    // ColorChecker white and middle gray in 8-bit sRGB (well-known values ~243 and ~122)
    const ChartSpec& cc = chartPresets()[0];
    Targets t = computeTargets(cc, *findIlluminant("D65"), TargetOptions{});
    auto to8 = [](double v) { return 255.0 * srgbEncode(v); };
    CHECK_NEAR(to8(t.linearRgb[18][1]), 243.0, 3.0);
    CHECK_NEAR(to8(t.linearRgb[21][1]), 122.0, 3.0);
    CHECK_NEAR(t.lab[21][0], 50.76, 0.05);  // lightness unchanged by adaptation of a neutral
}

static void testIlluminants() {
    cv::Vec2d a = planckianXy(2856);
    CHECK_NEAR(a[0], 0.44757, 0.002);
    CHECK_NEAR(a[1], 0.40745, 0.002);
    cv::Vec2d d65 = daylightXy(6504);
    CHECK_NEAR(d65[0], 0.31271, 0.002);
    CHECK_NEAR(d65[1], 0.32902, 0.002);
    CHECK_NEAR(findIlluminant("A")->cct, 2856, 30);
    CHECK_NEAR(findIlluminant("D65")->cct, 6504, 60);
}

static void testBoundedSolver() {
    // y = 2 x0 - 3 x1 + 0.5 x2, exactly representable
    std::mt19937 rng(1);
    std::uniform_real_distribution<double> u(0, 1);
    cv::Mat A(30, 3, CV_64F), b(30, 1, CV_64F);
    for (int i = 0; i < 30; ++i) {
        for (int j = 0; j < 3; ++j) A.at<double>(i, j) = u(rng);
        b.at<double>(i) = 2 * A.at<double>(i, 0) - 3 * A.at<double>(i, 1) + 0.5 * A.at<double>(i, 2);
    }
    const double inf = INFINITY;
    cv::Mat x = solveRowBounded(A, b, {-inf, -inf, -inf}, {inf, inf, inf}, {}, 0);
    CHECK_NEAR(x.at<double>(0), 2.0, 1e-9);
    CHECK_NEAR(x.at<double>(1), -3.0, 1e-9);
    // bounded to [-1, 1]: must stay inside
    x = solveRowBounded(A, b, {-1, -1, -1}, {1, 1, 1}, {}, 0);
    for (int j = 0; j < 3; ++j) CHECK(x.at<double>(j) >= -1 - 1e-12 && x.at<double>(j) <= 1 + 1e-12);
    // sum constraint
    x = solveRowBounded(A, b, {-inf, -inf, -inf}, {inf, inf, inf}, {1, 1, 1}, 1.0);
    CHECK_NEAR(x.at<double>(0) + x.at<double>(1) + x.at<double>(2), 1.0, 1e-9);
}

// Synthetic camera: cam = Minv * target (+ offset), with a known matrix.
struct Synthetic {
    std::vector<cv::Vec3d> cam, target, lab;
    std::vector<bool> neutral;
    std::vector<double> weight;
    cv::Matx33d camFromTarget;
};

static Synthetic makeSynthetic(double noise, cv::Vec3d offset = {0, 0, 0}) {
    const ChartSpec& cc = chartPresets()[0];
    Targets t = computeTargets(cc, *findIlluminant("D65"), TargetOptions{});
    Synthetic s;
    // a desaturating, color-cast camera response
    s.camFromTarget = cv::Matx33d(0.55, 0.30, 0.10, 0.20, 0.60, 0.15, 0.10, 0.25, 0.45);
    std::mt19937 rng(7);
    std::normal_distribution<double> n(0, noise);
    auto neutrals = cc.neutralIndices();
    for (int i = 0; i < cc.count(); ++i) {
        cv::Vec3d c = s.camFromTarget * t.linearRgb[i] + offset;
        for (int k = 0; k < 3; ++k) c[k] += noise > 0 ? n(rng) : 0;
        s.cam.push_back(c);
        s.target.push_back(t.linearRgb[i]);
        s.lab.push_back(t.lab[i]);
        s.neutral.push_back(std::find(neutrals.begin(), neutrals.end(), i) != neutrals.end());
        s.weight.push_back(1.0);
    }
    return s;
}

static void testFitRecoversMatrix() {
    Synthetic s = makeSynthetic(0.0);
    FitOptions o;
    o.model = Model::M3x3;
    o.separateGain = false;
    FitResult f = fitCcm(s.cam, s.target, s.lab, s.neutral, s.weight, o);
    CHECK(f.ok);
    cv::Matx33d expected = s.camFromTarget.inv();
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) CHECK_NEAR(f.gain * f.coef.at<double>(r, c), expected(r, c), 1e-6);

    // 3x4 recovers a black offset
    Synthetic so = makeSynthetic(0.0, {0.02, 0.03, 0.01});
    o.model = Model::M3x4;
    f = fitCcm(so.cam, so.target, so.lab, so.neutral, so.weight, o);
    Targets dummy{so.target, so.lab};
    std::vector<bool> inc(so.cam.size(), true);
    Evaluation ev = evaluate(f, so.cam, dummy, inc, so.neutral, "srgb", false);
    CHECK(ev.de00.max < 1e-3);

    // white-preserving: neutral target reproduced exactly on the weighted neutral sum
    Synthetic sn = makeSynthetic(0.003);
    o.model = Model::M3x3RowSum;
    f = fitCcm(sn.cam, sn.target, sn.lab, sn.neutral, sn.weight, o);
    CHECK(f.ok);
    cv::Vec3d xs(0, 0, 0), ts(0, 0, 0);
    for (size_t i = 0; i < sn.cam.size(); ++i)
        if (sn.neutral[i]) { xs += applyFit(f, sn.cam[i], false); ts += sn.target[i]; }
    CHECK_NEAR(cv::norm(xs - ts), 0.0, 1e-6);

    // WB + 3x3
    o.model = Model::WbM3x3;
    f = fitCcm(sn.cam, sn.target, sn.lab, sn.neutral, sn.weight, o);
    CHECK(f.ok);
    ev = evaluate(f, sn.cam, Targets{sn.target, sn.lab}, std::vector<bool>(sn.cam.size(), true), sn.neutral, "srgb", false);
    CHECK(ev.de00.mean < 2.0);
}

static void testConstraintsAndQuantization() {
    Synthetic s = makeSynthetic(0.002);
    FitOptions o;
    o.model = Model::M3x3;
    o.separateGain = true;
    o.boundCoefficients = true;
    o.coefMin = -1.5;
    o.coefMax = 1.5;
    FitResult f = fitCcm(s.cam, s.target, s.lab, s.neutral, s.weight, o);
    CHECK(f.ok);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) CHECK(std::abs(f.coef.at<double>(r, c)) <= 1.5 + 1e-9);

    // non-negative offset
    Synthetic so = makeSynthetic(0.002, {0.03, 0.03, 0.03});
    o = FitOptions{};
    o.model = Model::M3x4;
    o.offsetMode = OffsetMode::NonNegative;
    f = fitCcm(so.cam, so.target, so.lab, so.neutral, so.weight, o);
    for (int r = 0; r < 3; ++r) CHECK(f.coef.at<double>(r, 3) >= -1e-12);

    // quantization keeps row sums of the integer registers
    o = FitOptions{};
    o.model = Model::M3x3RowSum;
    o.quant.enabled = true;
    o.quant.intBits = 4;
    o.quant.fracBits = 6;
    f = fitCcm(s.cam, s.target, s.lab, s.neutral, s.weight, o);
    CHECK(f.quantized);
    for (int r = 0; r < 3; ++r) {
        double exact = 0;
        int q = 0;
        for (int c = 0; c < 3; ++c) {
            exact += f.coef.at<double>(r, c) * 64.0;
            q += f.coefRegisters.at<int>(r, c);
        }
        CHECK(q == int(std::round(exact)));
    }

    // ΔE2000 objective does not do worse than linear LS in ΔE
    Synthetic sd = makeSynthetic(0.004);
    o = FitOptions{};
    o.model = Model::M3x3;
    FitResult lin = fitCcm(sd.cam, sd.target, sd.lab, sd.neutral, sd.weight, o);
    o.objective = Objective::DeltaE2000;
    FitResult de = fitCcm(sd.cam, sd.target, sd.lab, sd.neutral, sd.weight, o);
    std::vector<bool> inc(sd.cam.size(), true);
    Targets tg{sd.target, sd.lab};
    const double e1 = evaluate(lin, sd.cam, tg, inc, sd.neutral, "srgb", false).de00.mean;
    const double e2 = evaluate(de, sd.cam, tg, inc, sd.neutral, "srgb", false).de00.mean;
    CHECK(e2 <= e1 + 1e-9);
}

// Render a perspective-distorted synthetic ColorChecker, sample it, fit, and check ΔE.
static void testEndToEndSyntheticImage() {
    const ChartSpec& cc = chartPresets()[0];
    Targets t = computeTargets(cc, *findIlluminant("D65"), TargetOptions{});
    const cv::Matx33d camFromTarget(0.55, 0.30, 0.10, 0.20, 0.60, 0.15, 0.10, 0.25, 0.45);
    const double black = 256.0, full = 4095.0 * 16;  // 12-bit data in 16-bit container with black 256*16

    cv::Mat img(900, 1200, CV_16UC3, cv::Scalar(black * 16, black * 16, black * 16));
    ChartCorners corners = {cv::Point2f(210, 180), cv::Point2f(1010, 220), cv::Point2f(980, 720), cv::Point2f(190, 690)};
    const cv::Matx33d h = gridToImage(corners, 4, 6);
    std::mt19937 rng(3);
    std::normal_distribution<double> n(0, 40.0);
    for (int y = 0; y < img.rows; ++y)
        for (int x = 0; x < img.cols; ++x) {
            cv::Vec3d g = h.inv() * cv::Vec3d(x, y, 1);
            const double gc = g[0] / g[2], gr = g[1] / g[2];
            const int c = int(std::round(gc)), r = int(std::round(gr));
            if (c < 0 || c > 5 || r < 0 || r > 3 || std::abs(gc - c) > 0.4 || std::abs(gr - r) > 0.4) continue;
            cv::Vec3d cam = camFromTarget * t.linearRgb[r * 6 + c] * 0.8;
            auto& px = img.at<cv::Vec3w>(y, x);
            for (int k = 0; k < 3; ++k)
                px[k] = cv::saturate_cast<ushort>(black * 16 + cam[k] * (full - black * 16) + n(rng));
        }

    SampleSettings ss;
    ss.rows = 4;
    ss.cols = 6;
    ss.fullScale = full;
    ss.black = cv::Vec3d(black * 16, black * 16, black * 16);
    auto stats = samplePatches(img, corners, ss);
    CHECK(stats.size() == 24);

    std::vector<bool> inc(24, true), neutral(24, false);
    for (int i : cc.neutralIndices()) neutral[i] = true;
    LinearityResult lin = analyzeLinearity(stats, cc, inc);
    CHECK(lin.ok);
    CHECK(lin.r2 > 0.999);
    CHECK_NEAR(lin.exponent, 1.0, 0.05);

    // black level recovered from the chart alone (sampled without black subtraction)
    SampleSettings s0 = ss;
    s0.black = cv::Vec3d(0, 0, 0);
    cv::Vec3d estBlack;
    CHECK(estimateBlackFromChart(samplePatches(img, corners, s0), t, inc, estBlack));
    for (int k = 0; k < 3; ++k) CHECK_NEAR(estBlack[k], black * 16, 0.002 * full);

    std::vector<cv::Vec3d> cam;
    for (auto& s : stats) cam.push_back(s.mean);
    FitOptions o;
    FitResult f = fitCcm(cam, t.linearRgb, t.lab, neutral, std::vector<double>(24, 1.0), o);
    Evaluation ev = evaluate(f, cam, t, inc, neutral, "srgb", false);
    std::printf("  e2e synthetic: mean dE00 %.3f max %.3f (WB-only baseline %.2f)\n", ev.de00.mean, ev.de00.max,
                ev.de00Baseline.mean);
    CHECK(ev.de00.mean < 0.5);
    CHECK(ev.de00Baseline.mean > 3.0);

    // Corners clicked in a rotated or mirrored order are recovered by rankOrientations.
    for (int flip = 0; flip < 2; ++flip)
        for (int turns = 0; turns < 4; ++turns) {
            ChartCorners wrong = rotateCorners(flip ? flipCorners(corners) : corners, turns);
            auto ranked = rankOrientations(img, wrong, ss, cc, t, {});
            double err = 0;
            for (int k = 0; k < 4; ++k) err += cv::norm(ranked.front().corners[k] - corners[k]);
            CHECK(err < 1e-3);
            CHECK(ranked.front().meanDeltaE < 0.5);
            CHECK(ranked[1].meanDeltaE > 5.0);
        }

    // A healthy camera has no inseparable pairs.
    auto sep = analyzeSeparability(stats, t, inc, neutral);
    CHECK(sep.issues.empty());
}

static void testReferenceLoader() {
    const std::string path = "ccm_test_ref.txt";
    FILE* f = std::fopen(path.c_str(), "w");
    std::fprintf(f, "CGATS.17\nNUMBER_OF_FIELDS 4\nBEGIN_DATA_FORMAT\nSAMPLE_ID LAB_L LAB_A LAB_B\nEND_DATA_FORMAT\n"
                    "NUMBER_OF_SETS 4\nBEGIN_DATA\nA1 10 1 2\nA2 20 3 4\nB1 30 5 6\nB2 40 7 8\nEND_DATA\n");
    std::fclose(f);
    ChartSpec c = makeCustomChart(2, 2);
    std::string err;
    CHECK(loadReferenceFile(path, true, c, err));
    // column-major: 2nd entry is (row 1, col 0)
    CHECK_NEAR(c.patches[2].labD50[0], 20.0, 1e-12);
    CHECK_NEAR(c.patches[1].labD50[0], 30.0, 1e-12);
    std::remove(path.c_str());
}

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"deltaE2000", testDeltaE2000},
        {"colorConversions", testColorConversions},
        {"illuminants", testIlluminants},
        {"boundedSolver", testBoundedSolver},
        {"fitRecoversMatrix", testFitRecoversMatrix},
        {"constraintsAndQuantization", testConstraintsAndQuantization},
        {"endToEndSyntheticImage", testEndToEndSyntheticImage},
        {"referenceLoader", testReferenceLoader},
    };
    for (const auto& [name, fn] : tests) {
        const int before = g_failures;
        fn();
        std::printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", name);
    }
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
