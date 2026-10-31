#include "core/CcmSolver.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/Targets.h"

namespace ccm {

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();
}

int featureCount(Model m) {
    switch (m) {
        case Model::M3x4: return 4;
        case Model::RootPoly3x6: return 6;
        default: return 3;
    }
}

bool hasOffset(Model m) { return m == Model::M3x4; }
bool hasRowSumConstraint(Model m) { return m == Model::M3x3RowSum || m == Model::WbM3x3; }

const char* modelName(Model m) {
    switch (m) {
        case Model::M3x3: return "3x3";
        case Model::M3x3RowSum: return "3x3 white-preserving";
        case Model::M3x4: return "3x3 + offset (3x4)";
        case Model::WbM3x3: return "WB gains + 3x3 white-preserving";
        case Model::RootPoly3x6: return "Root-polynomial 2nd order (3x6)";
    }
    return "?";
}

std::vector<double> features(Model m, const cv::Vec3d& v) {
    switch (m) {
        case Model::M3x4: return {v[0], v[1], v[2], 1.0};
        case Model::RootPoly3x6: {
            auto rp = [](double a, double b) { return std::sqrt(std::max(a * b, 0.0)); };
            return {v[0], v[1], v[2], rp(v[0], v[1]), rp(v[1], v[2]), rp(v[0], v[2])};
        }
        default: return {v[0], v[1], v[2]};
    }
}

static cv::Vec3d applyCoef(const FitResult& fit, const cv::Mat& coef, const cv::Vec3d& cam) {
    const cv::Vec3d x = cam.mul(fit.wbGains);
    const auto f = features(fit.model, x);
    cv::Vec3d out;
    for (int r = 0; r < 3; ++r) {
        double s = 0.0;
        for (int j = 0; j < coef.cols; ++j) s += coef.at<double>(r, j) * f[j];
        out[r] = fit.gain * s;
    }
    return out;
}

cv::Vec3d applyFit(const FitResult& fit, const cv::Vec3d& cam, bool useQuantized) {
    const cv::Mat& c = (useQuantized && fit.quantized) ? fit.coefQuantized : fit.coef;
    return applyCoef(fit, c, cam);
}

cv::Mat solveRowBounded(const cv::Mat& A, const cv::Mat& b, const std::vector<double>& lb,
                        const std::vector<double>& ub, const std::vector<double>& eq, double eqValue) {
    const int K = A.cols;
    CV_Assert(K <= 7 && int(lb.size()) == K && int(ub.size()) == K);
    const bool hasEq = !eq.empty();

    auto cost = [&](const cv::Mat& x) { return cv::norm(A * x - b, cv::NORM_L2SQR); };
    auto feasible = [&](const cv::Mat& x) {
        for (int j = 0; j < K; ++j) {
            const double v = x.at<double>(j), tol = 1e-9 * (1.0 + std::abs(v));
            if (v < lb[j] - tol || v > ub[j] + tol) return false;
        }
        return true;
    };

    // state per variable: 0 free, 1 at lower bound, 2 at upper bound
    std::vector<int> state(K, 0);
    cv::Mat best;
    double bestCost = kInf;
    while (true) {
        bool valid = true;
        for (int j = 0; j < K && valid; ++j)
            if ((state[j] == 1 && !std::isfinite(lb[j])) || (state[j] == 2 && !std::isfinite(ub[j]))) valid = false;

        if (valid) {
            cv::Mat x = cv::Mat::zeros(K, 1, CV_64F);
            std::vector<int> freeIdx;
            for (int j = 0; j < K; ++j) {
                if (state[j] == 0) freeIdx.push_back(j);
                else x.at<double>(j) = state[j] == 1 ? lb[j] : ub[j];
            }
            cv::Mat r = b - A * x;
            double s = eqValue;
            bool eqOnFree = false;
            if (hasEq)
                for (int j = 0; j < K; ++j) {
                    if (state[j] != 0) s -= eq[j] * x.at<double>(j);
                    else if (eq[j] != 0.0) eqOnFree = true;
                }

            bool ok = true;
            const int F = int(freeIdx.size());
            if (F > 0) {
                cv::Mat Af(A.rows, F, CV_64F);
                for (int k = 0; k < F; ++k) A.col(freeIdx[k]).copyTo(Af.col(k));
                cv::Mat xf;
                if (hasEq && eqOnFree) {
                    cv::Mat kkt = cv::Mat::zeros(F + 1, F + 1, CV_64F), rhs = cv::Mat::zeros(F + 1, 1, CV_64F);
                    cv::Mat ata = Af.t() * Af;
                    ata.copyTo(kkt(cv::Rect(0, 0, F, F)));
                    for (int k = 0; k < F; ++k) {
                        kkt.at<double>(k, F) = kkt.at<double>(F, k) = eq[freeIdx[k]];
                    }
                    cv::Mat atr = Af.t() * r;
                    atr.copyTo(rhs(cv::Rect(0, 0, 1, F)));
                    rhs.at<double>(F) = s;
                    cv::Mat sol;
                    ok = cv::solve(kkt, rhs, sol, cv::DECOMP_SVD);
                    xf = sol(cv::Rect(0, 0, 1, F));
                } else {
                    if (hasEq && std::abs(s) > 1e-9) ok = false;
                    if (ok) ok = cv::solve(Af, r, xf, cv::DECOMP_SVD);
                }
                if (ok)
                    for (int k = 0; k < F; ++k) x.at<double>(freeIdx[k]) = xf.at<double>(k);
            } else if (hasEq && std::abs(s) > 1e-9) {
                ok = false;
            }
            if (ok && feasible(x)) {
                const double c = cost(x);
                if (c < bestCost) {
                    bestCost = c;
                    best = x.clone();
                }
            }
        }

        int j = 0;
        while (j < K && ++state[j] == 3) state[j++] = 0;
        if (j == K) break;
    }

    if (best.empty()) {  // no feasible active set (e.g. equality incompatible with bounds)
        cv::solve(A, b, best, cv::DECOMP_SVD);
        for (int j = 0; j < K; ++j) best.at<double>(j) = std::clamp(best.at<double>(j), lb[j], ub[j]);
    }
    return best;
}

namespace {

struct Problem {
    std::vector<cv::Vec3d> x;    // camera RGB after WB
    std::vector<cv::Vec3d> t;    // targets / gain
    std::vector<cv::Vec3d> lab;  // target Lab
    std::vector<double> w;
};

double deltaECost(const FitResult& fit, const cv::Mat& coef, const Problem& p, const RgbSpace& space,
                  cv::Mat* residuals) {
    double total = 0.0;
    if (residuals) residuals->create(int(p.x.size()), 1, CV_64F);
    FitResult f = fit;
    f.wbGains = cv::Vec3d(1, 1, 1);  // p.x already white balanced
    for (size_t i = 0; i < p.x.size(); ++i) {
        const cv::Vec3d out = applyCoef(f, coef, p.x[i]);
        const double r = std::sqrt(p.w[i]) * deltaE2000(linearRgbToLab(out, space), p.lab[i]);
        if (residuals) residuals->at<double>(int(i)) = r;
        total += r * r;
    }
    return total;
}

// Alternating projection onto {row . eq == eqValue} and the box bounds.
void project(cv::Mat& coef, const std::vector<double>& lb, const std::vector<double>& ub,
             const std::vector<double>& eq, const cv::Vec3d& eqValue) {
    const int K = coef.cols;
    double eqNorm = 0.0;
    for (double e : eq) eqNorm += e * e;
    for (int it = 0; it < 50; ++it) {
        for (int r = 0; r < 3; ++r) {
            if (!eq.empty() && eqNorm > 0) {
                double d = -eqValue[r];
                for (int j = 0; j < K; ++j) d += eq[j] * coef.at<double>(r, j);
                for (int j = 0; j < K; ++j) coef.at<double>(r, j) -= d * eq[j] / eqNorm;
            }
            for (int j = 0; j < K; ++j) coef.at<double>(r, j) = std::clamp(coef.at<double>(r, j), lb[j], ub[j]);
        }
    }
}

void refineDeltaE(FitResult& fit, const Problem& p, const FitOptions& opt, const std::vector<double>& lb,
                  const std::vector<double>& ub, const std::vector<double>& eq, const cv::Vec3d& eqValue) {
    const RgbSpace& space = rgbSpace(opt.outputSpace);
    const int K = fit.coef.cols, P = 3 * K;
    cv::Mat coef = fit.coef.clone();
    cv::Mat r;
    double c = deltaECost(fit, coef, p, space, &r);
    double mu = 1e-3;
    for (int it = 0; it < opt.maxIterations; ++it) {
        cv::Mat J(r.rows, P, CV_64F);
        for (int k = 0; k < P; ++k) {
            const double h = 1e-6 * std::max(1.0, std::abs(coef.at<double>(k / K, k % K)));
            cv::Mat cp = coef.clone(), cm = coef.clone(), rp, rm;
            cp.at<double>(k / K, k % K) += h;
            cm.at<double>(k / K, k % K) -= h;
            deltaECost(fit, cp, p, space, &rp);
            deltaECost(fit, cm, p, space, &rm);
            cv::Mat col = (rp - rm) / (2 * h);
            col.copyTo(J.col(k));
        }
        cv::Mat jtj = J.t() * J, jtr = J.t() * r;
        bool improved = false;
        for (int tries = 0; tries < 10 && !improved; ++tries) {
            cv::Mat lhs = jtj + mu * cv::Mat::diag(jtj.diag() + 1e-12);
            cv::Mat step;
            if (!cv::solve(lhs, -jtr, step, cv::DECOMP_SVD)) break;
            cv::Mat cand = coef + step.reshape(1, 3);
            project(cand, lb, ub, eq, eqValue);
            cv::Mat rc;
            const double cc = deltaECost(fit, cand, p, space, &rc);
            if (cc < c) {
                improved = true;
                const double rel = (c - cc) / std::max(c, 1e-12);
                coef = cand;
                r = rc;
                c = cc;
                mu = std::max(mu / 3.0, 1e-9);
                if (rel < 1e-9) it = opt.maxIterations;
            } else {
                mu *= 4.0;
            }
        }
        if (!improved) break;
    }
    fit.coef = coef;
}

void quantize(FitResult& fit, const QuantOptions& q) {
    const int K = fit.coef.cols;
    const double scale = std::ldexp(1.0, q.fracBits);
    const double hi = std::ldexp(1.0, q.intBits + q.fracBits) - 1.0;
    const double lo = q.isSigned ? -std::ldexp(1.0, q.intBits + q.fracBits) : 0.0;
    const int matCols = std::min(K, 3) + (fit.model == Model::RootPoly3x6 ? 3 : 0);

    fit.coefRegisters = cv::Mat::zeros(3, matCols, CV_32S);
    fit.coefQuantized = fit.coef.clone();
    for (int r = 0; r < 3; ++r) {
        std::vector<double> exact(matCols);
        std::vector<int> qv(matCols);
        for (int j = 0; j < matCols; ++j) {
            exact[j] = fit.coef.at<double>(r, j) * scale;
            qv[j] = int(std::clamp(std::round(exact[j]), lo, hi));
        }
        if (q.preserveRowSum) {
            double want = 0.0;
            for (int j = 0; j < 3; ++j) want += exact[j];
            int diff = int(std::round(want)) - (qv[0] + qv[1] + qv[2]);
            while (diff != 0) {
                const int dir = diff > 0 ? 1 : -1;
                int best = -1;
                double bestGain = -kInf;
                for (int j = 0; j < 3; ++j) {
                    const double nv = qv[j] + dir;
                    if (nv < lo || nv > hi) continue;
                    const double gain = (exact[j] - qv[j]) * dir;  // prefer the element rounded the wrong way
                    if (gain > bestGain) {
                        bestGain = gain;
                        best = j;
                    }
                }
                if (best < 0) break;
                qv[best] += dir;
                diff -= dir;
            }
        }
        for (int j = 0; j < matCols; ++j) {
            fit.coefRegisters.at<int>(r, j) = qv[j];
            fit.coefQuantized.at<double>(r, j) = qv[j] / scale;
        }
    }

    fit.offsetDN.clear();
    if (hasOffset(fit.model)) {
        const double full = std::ldexp(1.0, q.offsetBits) - 1.0;
        for (int r = 0; r < 3; ++r) {
            const double b = fit.coef.at<double>(r, 3) * fit.gain;  // normalized output units
            const int dn = int(std::round(b * full));
            fit.offsetDN.push_back(dn);
            fit.coefQuantized.at<double>(r, 3) = dn / full / fit.gain;
        }
    }
    fit.quantized = true;
    fit.quant = q;
}

}  // namespace

FitResult fitCcm(const std::vector<cv::Vec3d>& cam, const std::vector<cv::Vec3d>& target,
                 const std::vector<cv::Vec3d>& targetLab, const std::vector<bool>& neutral,
                 const std::vector<double>& weight, const FitOptions& opt) {
    FitResult fit;
    fit.model = opt.model;
    const int K = featureCount(opt.model);

    std::vector<int> used, usedNeutral;
    for (size_t i = 0; i < cam.size(); ++i) {
        if (weight[i] <= 0) continue;
        used.push_back(int(i));
        if (neutral[i]) usedNeutral.push_back(int(i));
    }
    if (int(used.size()) < K + 1) {
        fit.message = "Need at least " + std::to_string(K + 1) + " usable patches for this model.";
        return fit;
    }
    const bool needNeutral = opt.model == Model::WbM3x3 || hasRowSumConstraint(opt.model);
    if (needNeutral && usedNeutral.empty()) {
        fit.message = "This model needs at least one neutral (gray) patch with a reference.";
        return fit;
    }
    const std::vector<int>& gainSet = usedNeutral.empty() ? used : usedNeutral;
    auto w = [&](int i) { return weight[i] * (neutral[i] ? opt.neutralWeight : 1.0); };

    // 1) white balance gains (WB model only)
    if (opt.model == Model::WbM3x3) {
        for (int c = 0; c < 3; ++c) {
            double num = 0, den = 0;
            for (int i : usedNeutral) {
                num += w(i) * target[i][c] * cam[i][c];
                den += w(i) * cam[i][c] * cam[i][c];
            }
            fit.wbGains[c] = den > 0 ? num / den : 1.0;
        }
    }

    // 2) exposure gain
    fit.gain = 1.0;
    if (opt.model != Model::WbM3x3 && (opt.separateGain || opt.model == Model::M3x3RowSum)) {
        double num = 0, den = 0;
        for (int i : gainSet) {
            const double mx = (cam[i][0] + cam[i][1] + cam[i][2]) / 3.0;
            const double mt = (target[i][0] + target[i][1] + target[i][2]) / 3.0;
            num += w(i) * mx * mt;
            den += w(i) * mx * mx;
        }
        if (den > 0 && num > 0) fit.gain = num / den;
    }

    Problem p;
    for (int i : used) {
        p.x.push_back(cam[i].mul(fit.wbGains));
        p.t.push_back(target[i] / fit.gain);
        p.lab.push_back(targetLab[i]);
        p.w.push_back(w(i));
    }
    const int n = int(p.x.size());

    // 3) bounds and white-preservation constraint
    std::vector<double> lb(K, -kInf), ub(K, kInf);
    for (int j = 0; j < K; ++j) {
        if (hasOffset(opt.model) && j == 3) {
            if (opt.offsetMode == OffsetMode::NonNegative) lb[j] = 0.0;
            continue;
        }
        if (opt.boundCoefficients) {
            lb[j] = opt.coefMin;
            ub[j] = opt.coefMax;
        }
    }
    std::vector<double> eq;
    cv::Vec3d eqValue(0, 0, 0);
    if (hasRowSumConstraint(opt.model)) {
        cv::Vec3d xs(0, 0, 0), ts(0, 0, 0);
        for (int k = 0; k < n; ++k) {
            if (!neutral[used[k]]) continue;
            xs += p.w[k] * p.x[k];
            ts += p.w[k] * p.t[k];
        }
        eq = features(opt.model, xs);
        eqValue = ts;
    }

    // 4) linear least squares per output row
    cv::Mat A(n, K, CV_64F);
    for (int k = 0; k < n; ++k) {
        const auto f = features(opt.model, p.x[k]);
        const double sw = std::sqrt(p.w[k]);
        for (int j = 0; j < K; ++j) A.at<double>(k, j) = sw * f[j];
    }
    fit.coef = cv::Mat(3, K, CV_64F);
    for (int r = 0; r < 3; ++r) {
        cv::Mat b(n, 1, CV_64F);
        for (int k = 0; k < n; ++k) b.at<double>(k) = std::sqrt(p.w[k]) * p.t[k][r];
        cv::Mat x = solveRowBounded(A, b, lb, ub, eq, eqValue[r]);
        cv::Mat(x.t()).copyTo(fit.coef.row(r));
    }

    // 5) optional ΔE2000 refinement
    if (opt.objective == Objective::DeltaE2000) {
        Problem pe = p;
        // ΔE is evaluated on gain * (M x), against the original target Lab
        FitResult tmp = fit;
        refineDeltaE(tmp, pe, opt, lb, ub, eq, eqValue);
        fit.coef = tmp.coef;
    }

    fit.ok = true;
    if (opt.quant.enabled) quantize(fit, opt.quant);
    return fit;
}

}  // namespace ccm
